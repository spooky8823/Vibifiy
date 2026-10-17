(function() {
    // Already injected into this document? (the Rust side may inject on every page-load event)
    if (window.__vibi_detector) return;
    window.__vibi_detector = true;

    // Heartbeat (unchanged behaviour). Placed after the guard so re-injection can't stack timers.
    setInterval(function() {
        document.title = 'vibi-alive-' + Date.now();
    }, 1000);

    // Kept for the Rust-side debug probe: hash of the last block handed to the backend.
    window.__vibi_last_hash = null;

    var POLL_MS = 500;       // how often we look at the page
    var STABLE_TICKS = 3;    // output must be unchanged for ~1.5s after streaming stops
    var CHUNK_SIZE = 1800;   // hex chars per URL-hash chunk
    var CHUNK_DELAY = 80;    // ms between chunks

    // A response only counts as "new" if we watched it being generated in THIS page session.
    // This is what stops old chats from being replayed when a page loads / you switch chats.
    var armed = false;
    var quiet = 0;
    var lastSig = '';
    var sent = {};           // blockKey -> true

    // ---------------------------------------------------------------- page probing

    function isStreaming() {
        return !!document.querySelector(
            '[data-testid="stop-button"], ' +
            'button[aria-label="Stop generating"], ' +
            'button[aria-label="Stop streaming"]'
        );
    }

    function simpleHash(str) {
        var hash = 0;
        for (var i = 0; i < str.length; i++) {
            hash = ((hash << 5) - hash + str.charCodeAt(i)) | 0;
        }
        return hash;
    }

    function lastAssistantMessage() {
        var msgs = document.querySelectorAll('[data-message-author-role="assistant"]');
        return msgs.length ? msgs[msgs.length - 1] : null;
    }

    // Text of one rendered code block. ChatGPT renders either <pre><code> or a CodeMirror viewer.
    function blockText(pre) {
        var code = pre.querySelector('code');
        if (code) return code.textContent || '';
        var cm = pre.querySelector('.cm-content');
        if (cm) return cm.innerText || cm.textContent || '';
        return pre.innerText || pre.textContent || '';
    }

    function isTaggedVibi(pre) {
        var code = pre.querySelector('code');
        var cls = code && code.className ? String(code.className) : '';
        return cls.indexOf('language-vibi') !== -1;
    }

    // Every ```vibi block in a message, in order. A block qualifies when it is tagged
    // `vibi` (or carries the "main vibi.claw" header) AND actually contains a job.
    function collectBlocks(msg) {
        var out = [];
        var pres = msg.querySelectorAll('pre');
        for (var i = 0; i < pres.length; i++) {
            var text = blockText(pres[i]);
            var hasHeader = text.indexOf('main vibi.claw') !== -1;
            if (!(isTaggedVibi(pres[i]) || hasHeader)) continue;
            if (text.indexOf('vibi.tool') === -1) continue;
            // Drop any UI chrome ("vibi", "Copy code", ...) that precedes the header.
            var at = text.indexOf('main vibi.claw');
            if (at > 0) text = text.slice(at);
            text = text.trim();
            if (text) out.push(text);
        }
        return out;
    }

    // ---------------------------------------------------------------- transfer to Rust

    function toHex(str) {
        var bytes = new TextEncoder().encode(str);
        var hex = '';
        for (var i = 0; i < bytes.length; i++) {
            hex += bytes[i].toString(16).padStart(2, '0');
        }
        return hex;
    }

    // Blocks are sent strictly one after another so chunks of two blocks never interleave.
    var outbox = [];
    var sending = false;

    function pump() {
        if (sending || outbox.length === 0) return;
        sending = true;

        var hex = outbox.shift();
        var chunks = [];
        for (var i = 0; i < hex.length; i += CHUNK_SIZE) {
            chunks.push(hex.substring(i, i + CHUNK_SIZE));
        }

        var base = window.location.pathname + window.location.search;
        var idx = 0;
        (function next() {
            if (idx < chunks.length) {
                history.replaceState(null, '', base + '#vibi-action-' + idx + '-' + chunks[idx]);
                idx++;
                setTimeout(next, CHUNK_DELAY);
                return;
            }
            history.replaceState(null, '', base + '#vibi-action-done');
            // Tidy the address bar once the backend has had time to read the marker.
            setTimeout(function() {
                if (window.location.hash === '#vibi-action-done') {
                    history.replaceState(null, '', base);
                }
            }, 400);
            sending = false;
            setTimeout(pump, 150);
        })();
    }

    function sendBlock(src) {
        outbox.push(toHex(src));
        pump();
    }

    // ---------------------------------------------------------------- main loop

    function dispatchBlocks(msg) {
        var msgId = msg.getAttribute('data-message-id') ||
            ('n' + document.querySelectorAll('[data-message-author-role="assistant"]').length);
        var blocks = collectBlocks(msg);
        for (var i = 0; i < blocks.length; i++) {
            var h = simpleHash(blocks[i]);
            var key = msgId + ':' + i + ':' + h;
            if (sent[key]) continue;
            sent[key] = true;
            window.__vibi_last_hash = h;
            sendBlock(blocks[i]);
        }
    }

    function tick() {
        if (isStreaming()) {
            armed = true;      // a response is being generated right now
            quiet = 0;
            lastSig = '';
            return;
        }
        if (!armed) return;    // nothing was generated since we loaded: never replay history

        var msg = lastAssistantMessage();
        if (!msg) return;

        // Wait until the finished message stops changing before reading it.
        var txt = msg.textContent || '';
        var sig = txt.length + ':' + simpleHash(txt);
        if (sig === lastSig) {
            quiet++;
        } else {
            quiet = 0;
            lastSig = sig;
        }
        if (quiet < STABLE_TICKS) return;

        armed = false;
        quiet = 0;
        lastSig = '';
        dispatchBlocks(msg);
    }

    setInterval(tick, POLL_MS);
})();
