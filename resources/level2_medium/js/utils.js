var Utils = (function() {
    'use strict';

    function debounce(func, wait) {
        var timeout;
        return function() {
            var context = this, args = arguments;
            clearTimeout(timeout);
            timeout = setTimeout(function() {
                func.apply(context, args);
            }, wait);
        };
    }

    function throttle(func, limit) {
        var inThrottle;
        return function() {
            var args = arguments, context = this;
            if (!inThrottle) {
                func.apply(context, args);
                inThrottle = true;
                setTimeout(function() { inThrottle = false; }, limit);
            }
        };
    }

    function formatBytes(bytes, decimals) {
        if (bytes === 0) return '0 Bytes';
        var k = 1024, dm = decimals || 2,
            sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB'],
            i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
    }

    function formatTime(ms) {
        if (ms < 1000) return ms.toFixed(1) + 'ms';
        return (ms / 1000).toFixed(2) + 's';
    }

    function calculateCompressionRatio(original, compressed) {
        if (original === 0) return 0;
        return ((original - compressed) / original * 100).toFixed(2);
    }

    function getFileExtension(filename) {
        return filename.slice((filename.lastIndexOf('.') - 1 >>> 0) + 2).toLowerCase();
    }

    function getFileType(filename) {
        var ext = getFileExtension(filename);
        var textExts = ['html', 'htm', 'css', 'js', 'json', 'xml', 'svg', 'txt', 'md'];
        var imageExts = ['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp'];
        if (textExts.indexOf(ext) !== -1) return 'text';
        if (imageExts.indexOf(ext) !== -1) return 'image';
        return 'binary';
    }

    function generateId() {
        return '_' + Math.random().toString(36).substr(2, 9);
    }

    function deepClone(obj) {
        return JSON.parse(JSON.stringify(obj));
    }

    function validateEmail(email) {
        var re = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
        return re.test(email);
    }

    function escapeHtml(text) {
        var div = document.createElement('div');
        div.appendChild(document.createTextNode(text));
        return div.innerHTML;
    }

    function parseQueryString(queryString) {
        var params = {}, queries, temp, i, l;
        queries = queryString.split('&');
        for (i = 0, l = queries.length; i < l; i++) {
            temp = queries[i].split('=');
            params[temp[0]] = temp[1] ? decodeURIComponent(temp[1]) : '';
        }
        return params;
    }

    function getScrollPercentage() {
        var h = document.documentElement,
            b = document.body,
            st = 'scrollTop',
            sh = 'scrollHeight';
        return ((h[st] || b[st]) / ((h[sh] || b[sh]) - h.clientHeight)) * 100;
    }

    function copyToClipboard(text) {
        if (navigator.clipboard && navigator.clipboard.writeText) {
            return navigator.clipboard.writeText(text);
        }
        var textArea = document.createElement('textarea');
        textArea.value = text;
        textArea.style.position = 'fixed';
        textArea.style.left = '-9999px';
        document.body.appendChild(textArea);
        textArea.select();
        try {
            document.execCommand('copy');
        } catch (err) {
            console.error('Copy failed:', err);
        }
        document.body.removeChild(textArea);
    }

    function isElementInViewport(el) {
        var rect = el.getBoundingClientRect();
        return (
            rect.top >= 0 &&
            rect.left >= 0 &&
            rect.bottom <= (window.innerHeight || document.documentElement.clientHeight) &&
            rect.right <= (window.innerWidth || document.documentElement.clientWidth)
        );
    }

    function easeInOutCubic(t) {
        return t < 0.5
            ? 4 * t * t * t
            : 1 - Math.pow(-2 * t + 2, 3) / 2;
    }

    function lerp(start, end, factor) {
        return start + (end - start) * factor;
    }

    return {
        debounce: debounce,
        throttle: throttle,
        formatBytes: formatBytes,
        formatTime: formatTime,
        calculateCompressionRatio: calculateCompressionRatio,
        getFileExtension: getFileExtension,
        getFileType: getFileType,
        generateId: generateId,
        deepClone: deepClone,
        validateEmail: validateEmail,
        escapeHtml: escapeHtml,
        parseQueryString: parseQueryString,
        getScrollPercentage: getScrollPercentage,
        copyToClipboard: copyToClipboard,
        isElementInViewport: isElementInViewport,
        easeInOutCubic: easeInOutCubic,
        lerp: lerp
    };
})();
