// WebCompress Pro - Main Application Entry Point
var App = (function() {
    var initialized = false;

    function init() {
        if (initialized) return;
        initialized = true;

        console.log('WebCompress Pro v1.0.0 - Initializing...');

        initHeader();
        initSmoothScroll();
        initForms();
        initAnimations();
        Analytics.init();

        if (typeof Router !== 'undefined') {
            Router.start();
        }

        console.log('WebCompress Pro - Ready ✓');

        Store.addNotification('应用初始化完成', 'success');
    }

    function initHeader() {
        var header = document.querySelector('.app-header');
        if (!header) return;

        var lastScrollY = 0;
        var ticking = false;

        window.addEventListener('scroll', Utils.throttle(function() {
            var scrollY = window.pageYOffset;

            if (scrollY > 100) {
                header.style.boxShadow = '0 2px 16px rgba(0,0,0,0.08)';
            } else {
                header.style.boxShadow = 'none';
            }

            lastScrollY = scrollY;
        }, 100));
    }

    function initSmoothScroll() {
        document.querySelectorAll('a[href^="#"]').forEach(function(anchor) {
            anchor.addEventListener('click', function(e) {
                var href = this.getAttribute('href');
                if (href === '#' || href.length < 2) return;

                e.preventDefault();
                var target = document.querySelector(href);
                if (target) {
                    Utils.scrollToElement(target);
                }
            });
        });
    }

    function initForms() {
        document.querySelectorAll('form').forEach(function(form) {
            form.addEventListener('submit', function(e) {
                e.preventDefault();

                var formData = new FormData(form);
                var data = {};
                formData.forEach(function(val, key) { data[key] = val; });

                console.log('Form submitted:', data);
                Store.addNotification('表单已提交', 'success');

                var btn = form.querySelector('[type="submit"]');
                if (btn) {
                    btn.textContent = '已提交 ✓';
                    btn.disabled = true;
                    btn.style.background = '#00b894';
                    setTimeout(function() {
                        btn.textContent = '提交';
                        btn.disabled = false;
                        btn.style.background = '';
                    }, 2000);
                }
            });
        });
    }

    function initAnimations() {
        var observerOptions = { threshold: 0.15, rootMargin: '0px 0px -40px 0px' };

        var observer = new IntersectionObserver(function(entries) {
            entries.forEach(function(entry) {
                if (entry.isIntersecting) {
                    entry.target.classList.add('animate-fade-in');
                    observer.unobserve(entry.target);
                }
            });
        }, observerOptions);

        document.querySelectorAll('.feature-block, .product-card, .blog-post-preview, .dash-card')
            .forEach(function(el, idx) {
                el.style.opacity = '0';
                el.style.transitionDelay = (idx % 4) * 80 + 'ms';
                observer.observe(el);
            });
    }

    function showLoading(container) {
        container.innerHTML =
            '<div class="skeleton skeleton-title"></div>' +
            '<div class="skeleton skeleton-text"></div>' +
            '<div class="skeleton skeleton-text" style="width:80%"></div>' +
            '<div class="skeleton skeleton-img"></div>';
    }

    function hideLoading(container, html) {
        container.innerHTML = html;
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }

    return {
        init: init,
        showLoading: showLoading,
        hideLoading: hideLoading
    };
})();
