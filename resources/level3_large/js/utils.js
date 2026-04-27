// WebCompress Pro - Utility Functions
var Utils = (function() {
    function $(selector) { return document.querySelector(selector); }
    function $$(selector) { return document.querySelectorAll(selector); }

    function debounce(fn, delay) {
        var timer;
        return function() {
            var ctx = this, args = arguments;
            clearTimeout(timer);
            timer = setTimeout(function() { fn.apply(ctx, args); }, delay || 300);
        };
    }

    function throttle(fn, limit) {
        var inThrottle;
        return function() {
            if (!inThrottle) {
                fn.apply(this, arguments);
                inThrottle = true;
                setTimeout(function() { inThrottle = false; }, limit || 200);
            }
        };
    }

    function formatBytes(bytes) {
        if (bytes === 0) return '0 B';
        var k = 1024, sizes = ['B', 'KB', 'MB', 'GB'];
        var i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    }

    function formatPercent(value) {
        return value.toFixed(1) + '%';
    }

    function formatDate(dateStr) {
        var d = new Date(dateStr);
        return d.getFullYear() + '-' +
            String(d.getMonth() + 1).padStart(2, '0') + '-' +
            String(d.getDate()).padStart(2, '0');
    }

    function generateId(prefix) {
        return (prefix || 'wc') + '_' + Math.random().toString(36).substr(2, 9);
    }

    function deepClone(obj) {
        return JSON.parse(JSON.stringify(obj));
    }

    function escapeHtml(str) {
        var div = document.createElement('div');
        div.appendChild(document.createTextNode(str));
        return div.innerHTML;
    }

    function getQueryParam(name) {
        var match = location.search.match(new RegExp('[?&]' + name + '=([^&]*)'));
        return match ? decodeURIComponent(match[1]) : null;
    }

    function copyToClipboard(text) {
        if (navigator.clipboard) {
            navigator.clipboard.writeText(text).then(function() {
                console.log('Copied to clipboard');
            });
        } else {
            var input = document.createElement('input');
            input.value = text; document.body.appendChild(input);
            input.select(); document.execCommand('copy');
            document.body.removeChild(input);
        }
    }

    function scrollToElement(el, offset) {
        offset = offset || 80;
        var top = el.getBoundingClientRect().top + window.pageYOffset - offset;
        window.scrollTo({ top: top, behavior: 'smooth' });
    }

    function isVisible(el) {
        var rect = el.getBoundingClientRect();
        return rect.top < window.innerHeight && rect.bottom > 0;
    }

    return {
        $: $$,
        $$: $$,
        debounce: debounce,
        throttle: throttle,
        formatBytes: formatBytes,
        formatPercent: formatPercent,
        formatDate: formatDate,
        generateId: generateId,
        deepClone: deepClone,
        escapeHtml: escapeHtml,
        getQueryParam: getQueryParam,
        copyToClipboard: copyToClipboard,
        scrollToElement: scrollToElement,
        isVisible: isVisible
    };
})();
