(function() {
    setInterval(function() {
        document.title = 'vibi-alive-' + Date.now();
    }, 1000);
    
    if (window.__vibi_detector) return;
    window.__vibi_detector = true;
    
    function simpleHash(str) {
        var hash = 0;
        for (var i = 0; i < str.length; i++) {
            hash = ((hash << 5) - hash + str.charCodeAt(i)) | 0;
        }
        return hash;
    }
    
    function scanForVibiBlocks() {
        var allElements = document.querySelectorAll('pre code, code, pre');
        var vibiCode = null;
        
        for (var i = allElements.length - 1; i >= 0; i--) {
            var txt = allElements[i].textContent || '';
            if (txt.indexOf('main vibi.claw') > -1 || txt.indexOf('import vibi.tools') > -1) {
                vibiCode = txt.trim();
                break;
            }
        }
        
        if (!vibiCode) return;
        
        var hash = simpleHash(vibiCode);
        if (hash === window.__vibi_last_hash) return;
        window.__vibi_last_hash = hash;
        
        document.title = 'vibi-dbg-gemini-MATCH:' + vibiCode.length;
        
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