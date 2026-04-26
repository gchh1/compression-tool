var Animation = (function() {
    'use strict';

    var animationFrameId = null;

    function animateCounter(element, target, duration) {
        if (!element) return;
        var start = 0, startTime = null;

        function step(timestamp) {
            if (!startTime) startTime = timestamp;
            var progress = Math.min((timestamp - startTime) / duration, 1);
            var eased = Utils.easeInOutCubic(progress);
            element.textContent = Math.floor(eased * target);

            if (progress < 1) {
                animationFrameId = requestAnimationFrame(step);
            }
        }

        if (animationFrameId) cancelAnimationFrame(animationFrameId);
        animationFrameId = requestAnimationFrame(step);
    }

    function initScrollAnimations() {
        var observerOptions = {
            threshold: 0.2,
            rootMargin: '0px 0px -50px 0px'
        };

        var observer = new IntersectionObserver(function(entries) {
            entries.forEach(function(entry) {
                if (entry.isIntersecting) {
                    entry.target.classList.add('animate-in');
                    if (entry.target.classList.contains('stat-number')) {
                        var target = parseInt(entry.target.getAttribute('data-target'), 10);
                        animateCounter(entry.target, target, 1500);
                    }
                }
            });
        }, observerOptions);

        document.querySelectorAll('.feature-card, .stat-item').forEach(function(el) {
            el.style.opacity = '0';
            el.style.transform = 'translateY(30px)';
            el.style.transition = 'opacity 0.6s ease, transform 0.6s ease';
            observer.observe(el);
        });
    }

    function addAnimateInStyles() {
        var style = document.createElement('style');
        style.textContent = '.animate-in { opacity: 1 !important; transform: translateY(0) !important; }';
        document.head.appendChild(style);
    }

    function initParallaxHero() {
        var heroBanner = document.querySelector('.hero-banner');
        var heroContent = document.querySelector('.hero-content');
        if (!heroBanner || !heroContent) return;

        window.addEventListener('scroll', Utils.throttle(function() {
            var scrolled = window.pageYOffset;
            var rate = scrolled * 0.4;
            heroContent.style.transform = 'translate(-50%, calc(-50% + ' + rate + 'px))';
        }, 16));
    }

    function initNavHighlight() {
        var sections = document.querySelectorAll('section[id]');
        var navItems = document.querySelectorAll('.nav-item');

        window.addEventListener('scroll', Utils.throttle(function() {
            var scrollPos = window.scrollY + 100;

            sections.forEach(function(section) {
                var top = section.offsetTop;
                var height = section.offsetHeight;
                var id = section.getAttribute('id');

                if (scrollPos >= top && scrollPos < top + height) {
                    navItems.forEach(function(item) {
                        item.classList.remove('active');
                        if (item.getAttribute('href') === '#' + id) {
                            item.classList.add('active');
                        }
                    });
                }
            });
        }, 100));
    }

    function fadeInOnLoad() {
        document.body.style.opacity = '0';
        document.body.style.transition = 'opacity 0.5s ease';

        requestAnimationFrame(function() {
            document.body.style.opacity = '1';
        });

        var header = document.querySelector('.site-header');
        if (header) {
            header.style.transform = 'translateY(-100%)';
            header.style.transition = 'transform 0.4s ease';
            requestAnimationFrame(function() {
                header.style.transform = 'translateY(0)';
            });
        }
    }

    function rippleEffect(e, element) {
        var rect = element.getBoundingClientRect();
        var x = e.clientX - rect.left;
        var y = e.clientY - rect.top;
        var ripple = document.createElement('span');
        ripple.className = 'ripple-effect';
        ripple.style.cssText =
            'position:absolute;border-radius:50%;background:rgba(255,255,255,0.3);' +
            'transform:scale(0);animation:ripple 0.6s linear;left:' + x + 'px;top:' + y + 'px;' +
            'width:200px;height:200px;margin-left:-100px;margin-top:-100px;' +
            'pointer-events:none;';
        element.style.position = 'relative';
        element.style.overflow = 'hidden';
        element.appendChild(ripple);
        setTimeout(function() { ripple.remove(); }, 600);
    }

    function typewriterEffect(element, text, speed) {
        var i = 0;
        element.textContent = '';

        function type() {
            if (i < text.length) {
                element.textContent += text.charAt(i);
                i++;
                setTimeout(type, speed);
            }
        }

        type();
    }

    function shakeElement(element) {
        element.style.animation = 'shake 0.5s ease-in-out';
        setTimeout(function() {
            element.style.animation = '';
        }, 500);
    }

    function pulseElement(element) {
        element.style.animation = 'pulse 1s ease-in-out infinite';
    }

    function stopPulse(element) {
        element.style.animation = '';
    }

    return {
        init: function() {
            addAnimateInStyles();
            initScrollAnimations();
            initParallaxHero();
            initNavHighlight();
            fadeInOnLoad();

            var style = document.createElement('style');
            style.textContent =
                '@keyframes ripple{to{transform:scale(4);opacity:0;}}' +
                '@keyframes shake{0%,100%{transform:translateX(0);}25%{transform:translateX(-10px);}' +
                '75%{transform:translateX(10px);}}' +
                '@keyframes pulse{0%,100%{transform:scale(1);}50%{transform:scale(1.05);}}';
            document.head.appendChild(style);
        },
        animateCounter: animateCounter,
        rippleEffect: rippleEffect,
        typewriterEffect: typewriterEffect,
        shakeElement: shakeElement,
        pulseElement: pulseElement,
        stopPulse: stopPulse
    };
})();
