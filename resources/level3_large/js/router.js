// WebCompress Pro - Simple Client-Side Router
var Router = (function() {
    var routes = [];
    var currentRoute = null;

    function register(path, handler) {
        routes.push({ path: path, handler: handler, regex: new RegExp('^' + path.replace(/:[^/]+/g, '([^/]+)') + '$') });
    }

    function navigate(path) {
        window.history.pushState(null, '', path);
        handleRoute(path);
    }

    function handleRoute(path) {
        path = path || location.pathname;

        for (var i = 0; i < routes.length; i++) {
            var route = routes[i];
            var match = path.match(route.regex);
            if (match) {
                var params = route.path.split('/').filter(function(p) { return p.startsWith(':'); })
                    .map(function(_, idx) { return match[idx + 1]; });

                currentRoute = { path: route.path, params: params };
                route.handler.apply(null, params);
                return;
            }
        }

        if (routes.length > 0) {
            routes[0].handler();
        }
    }

    function getCurrentRoute() {
        return currentRoute;
    }

    function start() {
        window.addEventListener('popstate', function() {
            handleRoute(location.pathname);
        });

        document.addEventListener('click', function(e) {
            var link = e.target.closest('a[href]');
            if (link && link.getAttribute('href').startsWith('/') &&
                !link.hasAttribute('target') && !e.ctrlKey && !e.metaKey) {
                e.preventDefault();
                navigate(link.getAttribute('href'));
            }
        });

        handleRoute(location.pathname);
    }

    return {
        register: register,
        navigate: navigate,
        getCurrentRoute: getCurrentRoute,
        start: start
    };
});
