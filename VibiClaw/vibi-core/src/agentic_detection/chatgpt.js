(function() {
    setInterval(function() {
        document.title = 'vibi-alive-' + Date.now();
    }, 1000);

    if (window.__vibi_detector) return;
    window.__vibi_detector = true;

    function isStreaming() {
        return !!document.querySelector('[data-testid="stop-button"]');
    }

    function findVibiCodeBlock(msgEl) {
        var pres = msgEl.querySelectorAll('pre');
        var fallback = null;

        for (var i = 0; i < pres.length; i++) {
            var pre = pres[i];
            var codeEl = pre.querySelector('code');
            if (!codeEl) continue;

            if (!fallback) fallback = codeEl;

            // look for a language label near this pre (header row above it)
            var container = pre.closest('div');
            var wrapper = container ? container.parentElement : null;
            var text = wrapper ? wrapper.textContent : '';

            // check code element's own class too, in case it exists
            var cls = codeEl.className || '';
            if (/\bvibi\b/i.test(cls)) return codeEl;

            // check preceding sibling / header text for exact "vibi" label
            var prev = pre.previousElementSibling;
            if (prev && prev.textContent && prev.textContent.trim().toLowerCase() === 'vibi') {
                return codeEl;
            }

            // check any ancestor up to 3 levels for a small header node saying "vibi"
            var anc = pre.parentElement;
            for (var d = 0; d < 3 && anc; d++) {
                var headerCandidates = anc.querySelectorAll('span, div');
                for (var h = 0; h < headerCandidates.length; h++) {
                    var t = headerCandidates[h].textContent;
                    if (t && t.trim().toLowerCase() === 'vibi' && headerCandidates[h].children.length === 0) {
                        return codeEl;
                    }
                }
                anc = anc.parentElement;
            }
        }
        return fallback; // last resort: first code block found
    }

    function simpleHash(str) {
        var hash = 0;
        for (var i = 0; i < str.length; i++) {
            hash = ((hash << 5) - hash + str.charCodeAt(i)) | 0;
        }
        return hash;
    }

    function scanForVibiBlocks() {
        var messages = document.querySelectorAll('[data-message-author-role="assistant"]');
        if (messages.length === 0) return;

        var lastMsg = messages[messages.length - 1];

        // don't grab a partial block mid-stream
        if (isStreaming()) return;

        var codeEl = findVibiCodeBlock(lastMsg);
        if (!codeEl) return;

        var vibiCode = codeEl.textContent.trim();
        if (!vibiCode) return;

        var hash = simpleHash(vibiCode);
        if (hash === window.__vibi_last_hash) return; // already sent this exact block
        window.__vibi_last_hash = hash;

        var bytes = new TextEncoder().encode(vibiCode);
        var hex = '';
        for (var i = 0; i < bytes.length; i++) {
            hex += bytes[i].toString(16).padStart(2, '0');
        }

        var chunkSize = 1800;
        var chunks = [];
        for (var i = 0; i < hex.length; i += chunkSize) {
            chunks.push(hex.substring(i, i + chunkSize));
        }

        var idx = 0;
        function sendChunk() {
            if (idx < chunks.length) {
                history.replaceState(null, '', window.location.pathname + window.location.search + '#vibi-action-' + idx + '-' + chunks[idx]);
                idx++;
                setTimeout(sendChunk, 80);
            } else {
                history.replaceState(null, '', window.location.pathname + window.location.search + '#vibi-action-done');
            }
        }
        sendChunk();
    }

    setInterval(scanForVibiBlocks, 1000);
})();