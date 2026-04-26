// WebCompress Pro - Local State Store (Simple Flux-like)
var Store = (function() {
    var state = {
        user: null,
        settings: { algorithm: 'deflate', quality: 85, theme: 'light' },
        compressionHistory: [],
        ui: { sidebarOpen: true, activeTab: 'compress' },
        notifications: []
    };

    var listeners = [];

    function getState() {
        return state;
    }

    function setState(path, value) {
        var keys = path.split('.');
        var obj = state;
        for (var i = 0; i < keys.length - 1; i++) {
            if (!obj[keys[i]]) obj[keys[i]] = {};
            obj = obj[keys[i]];
        }
        obj[keys[keys.length - 1]] = value;
        notifyListeners(path);
        persistState();
    }

    function subscribe(listener) {
        listeners.push(listener);
        return function() {
            listeners = listeners.filter(function(l) { return l !== listener; });
        };
    }

    function notifyListeners(changedPath) {
        listeners.forEach(function(listener) {
            listener(state, changedPath);
        });
    }

    function addNotification(msg, type) {
        var notification = {
            id: Date.now(),
            message: msg,
            type: type || 'info',
            timestamp: new Date().toISOString()
        };
        state.notifications.unshift(notification);
        if (state.notifications.length > 50) {
            state.notifications.pop();
        }
        notifyListeners('notifications');
    }

    function removeNotification(id) {
        state.notifications = state.notifications.filter(function(n) {
            return n.id !== id;
        });
        notifyListeners('notifications');
    }

    function addToHistory(entry) {
        entry.id = Date.now();
        entry.timestamp = new Date().toISOString();
        state.compressionHistory.unshift(entry);
        if (state.compressionHistory.length > 100) {
            state.compressionHistory.pop();
        }
        notifyListeners('compressionHistory');
    }

    function clearHistory() {
        state.compressionHistory = [];
        notifyListeners('compressionHistory');
    }

    function persistState() {
        try {
            var saveable = {
                settings: state.settings,
                compressionHistory: state.compressionHistory.slice(0, 20)
            };
            localStorage.setItem('wc_state', JSON.stringify(saveable));
        } catch(e) {}
    }

    function loadState() {
        try {
            var saved = JSON.parse(localStorage.getItem('wc_state'));
            if (saved) {
                if (saved.settings) state.settings = saved.settings;
                if (saved.compressionHistory) state.compressionHistory = saved.compressionHistory;
            }
        } catch(e) {}
    }

    loadState();

    return {
        getState: getState,
        setState: setState,
        subscribe: subscribe,
        addNotification: addNotification,
        removeNotification: removeNotification,
        addToHistory: addToHistory,
        clearHistory: clearHistory,
        persistState: persistState
    };
});
