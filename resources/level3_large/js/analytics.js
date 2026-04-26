// WebCompress Pro - Analytics & Tracking
var Analytics = (function() {
    var events = [];
    var startTime = Date.now();

    function track(eventName, data) {
        var eventObj = {
            name: eventName,
            timestamp: new Date().toISOString(),
            data: data || {},
            sessionId: getSessionId(),
            url: location.pathname
        };
        events.push(eventObj);
        console.log('[Analytics]', eventName, data);

        if (events.length >= 10) {
            flush();
        }
    }

    function trackPageView() {
        track('page_view', {
            title: document.title,
            referrer: document.referrer,
            userAgent: navigator.userAgent.substring(0, 120)
        });
    }

    function trackEvent(category, action, label) {
        track('custom_event', {
            category: category,
            action: action,
            label: label
        });
    }

    function trackPerformance() {
        if (window.performance && performance.timing) {
            var t = performance.timing;
            track('performance_metrics', {
                domContentLoaded: t.domContentLoadedEventEnd - t.navigationStart,
                pageLoadComplete: t.loadEventEnd - t.navigationStart,
                firstPaint: t.responseStart - t.requestStart
            });
        }
    }

    function getSessionId() {
        var sid = localStorage.getItem('wc_session_id');
        if (!sid) {
            sid = 'sess_' + Date.now().toString(36) + Math.random().toString(36).substr(2, 6);
            localStorage.setItem('wc_session_id', sid);
        }
        return sid;
    }

    function flush() {
        if (events.length === 0) return;
        var payload = events.slice();
        events = [];

        try {
            var existing = JSON.parse(localStorage.getItem('wc_analytics') || '[]');
            existing.push.apply(existing, payload);
            localStorage.setItem('wc_analytics', JSON.stringify(existing));
        } catch(e) {
            console.warn('Analytics storage full, clearing old data');
            localStorage.removeItem('wc_analytics');
        }
    }

    function getReport() {
        return JSON.parse(localStorage.getItem('wc_analytics') || '[]');
    }

    function init() {
        trackPageView();

        window.addEventListener('beforeunload', function() {
            trackPerformance();
            flush();
        });

        setInterval(function() {
            if (events.length > 0) flush();
        }, 5000);
    }

    return {
        track: track,
        trackPageView: trackPageView,
        trackEvent: trackEvent,
        trackPerformance: trackPerformance,
        flush: flush,
        getReport: getReport,
        init: init
    };
})();
