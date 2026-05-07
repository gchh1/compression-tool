document.addEventListener('DOMContentLoaded', function() {
    var navLinks = document.querySelectorAll('.nav-link');
    navLinks.forEach(function(link) {
        link.addEventListener('click', function(e) {
            var href = this.getAttribute('href');
            if (href && href.startsWith('#')) {
                e.preventDefault();
                var target = document.querySelector(href);
                if (target) {
                    target.scrollIntoView({ behavior: 'smooth' });
                }
                navLinks.forEach(function(l) { l.classList.remove('active'); });
                this.classList.add('active');
            }
        });
    });

    var galleryItems = document.querySelectorAll('.gallery-item');
    galleryItems.forEach(function(item) {
        item.addEventListener('click', function() {
            var img = this.querySelector('img');
            if (img) {
                var overlay = document.createElement('div');
                overlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.85);display:flex;align-items:center;justify-content:center;z-index:1000;cursor:pointer;';
                var bigImg = document.createElement('img');
                bigImg.src = img.src;
                bigImg.style.cssText = 'max-width:90%;max-height:90%;border-radius:8px;box-shadow:0 8px 32px rgba(0,0,0,0.5);';
                overlay.appendChild(bigImg);
                overlay.addEventListener('click', function() { document.body.removeChild(overlay); });
                document.body.appendChild(overlay);
            }
        });
    });
});
