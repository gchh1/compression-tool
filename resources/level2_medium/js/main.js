(function() {
    'use strict';

    document.addEventListener('DOMContentLoaded', function() {
        App.init();
    });

    var App = {
        init: function() {
            this.initSmoothScroll();
            this.initContactForm();
            this.initHeaderScroll();
            this.initLazyImages();
            Animation.init();
            console.log('WebCompress Level 2 - Initialized');
        },

        initSmoothScroll: function() {
            document.querySelectorAll('a[href^="#"]').forEach(function(anchor) {
                anchor.addEventListener('click', function(e) {
                    var targetId = this.getAttribute('href');
                    if (targetId === '#') return;

                    e.preventDefault();
                    var target = document.querySelector(targetId);
                    if (target) {
                        var headerOffset = 80;
                        var elementPosition = target.getBoundingClientRect().top;
                        var offsetPosition = elementPosition + window.pageYOffset - headerOffset;

                        window.scrollTo({
                            top: offsetPosition,
                            behavior: 'smooth'
                        });
                    }
                });
            });
        },

        initContactForm: function() {
            var form = document.getElementById('contactForm');
            if (!form) return;

            form.addEventListener('submit', function(e) {
                e.preventDefault();

                if (!this.checkValidity()) {
                    this.reportValidity();
                    return;
                }

                var formData = new FormData(this);
                var data = {};
                formData.forEach(function(value, key) {
                    data[key] = value;
                });

                var submitBtn = form.querySelector('.btn-primary');
                submitBtn.textContent = '发送中...';
                submitBtn.disabled = true;

                setTimeout(function() {
                    alert('消息已成功发送！感谢您的反馈。');
                    form.reset();
                    submitBtn.textContent = '发送消息';
                    submitBtn.disabled = false;
                }, 1200);
            });

            var inputs = form.querySelectorAll('input, textarea, select');
            inputs.forEach(function(input) {
                input.addEventListener('focus', function() {
                    this.parentElement.classList.add('focused');
                });
                input.addEventListener('blur', function() {
                    this.parentElement.classList.remove('focused');
                    if (this.value.trim()) {
                        this.parentElement.classList.add('filled');
                    } else {
                        this.parentElement.classList.remove('filled');
                    }
                });
            });
        },

        initHeaderScroll: function() {
            var header = document.querySelector('.site-header');
            if (!header) return;

            var lastScrollY = 0;

            window.addEventListener('scroll', Utils.throttle(function() {
                var currentScrollY = window.pageYOffset;

                if (currentScrollY > 100) {
                    header.classList.add('scrolled');
                    header.style.boxShadow = '0 4px 20px rgba(0,0,0,0.1)';
                } else {
                    header.classList.remove('scrolled');
                    header.style.boxShadow = '';
                }

                if (currentScrollY > lastScrollY && currentScrollY > 200) {
                    header.style.transform = 'translateY(-100%)';
                } else {
                    header.style.transform = 'translateY(0)';
                }

                lastScrollY = currentScrollY;
            }, 50));
        },

        initLazyImages: function() {
            if ('IntersectionObserver' in window) {
                var imageObserver = new IntersectionObserver(function(entries) {
                    entries.forEach(function(entry) {
                        if (entry.isIntersecting) {
                            var img = entry.target;
                            if (img.dataset.src) {
                                img.src = img.dataset.src;
                                img.removeAttribute('data-src');
                            }
                            imageObserver.unobserve(img);
                        }
                    });
                }, { rootMargin: '200px' });

                document.querySelectorAll('img[data-src]').forEach(function(img) {
                    imageObserver.observe(img);
                });
            }
        }
    };
})();
