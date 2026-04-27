// WebCompress Pro - Reusable UI Components
var Components = (function() {

    function createButton(label, variant, onClick) {
        var btn = document.createElement('button');
        btn.textContent = label;
        btn.className = 'btn-' + (variant || 'primary');
        if (onClick) btn.addEventListener('click', onClick);
        return btn;
    }

    function createModal(title, content, actions) {
        var overlay = document.createElement('div');
        overlay.className = 'modal-overlay';
        overlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.5);z-index:9999;display:flex;align-items:center;justify-content:center;opacity:0;transition:opacity 0.25s;';

        var modal = document.createElement('div');
        modal.className = 'modal-content';
        modal.style.cssText = 'background:#fff;border-radius:16px;padding:32px;max-width:560px;width:90%;max-height:80vh;overflow-y:auto;box-shadow:0 20px 60px rgba(0,0,0,0.3);transform:scale(0.95);transition:transform 0.25s;';

        var header = document.createElement('div');
        header.innerHTML = '<h3 style="margin-bottom:16px;font-size:20px;">' + title + '</h3>';

        var body = document.createElement('div');
        body.className = 'modal-body';
        if (typeof content === 'string') {
            body.innerHTML = content;
        } else if (content instanceof HTMLElement) {
            body.appendChild(content);
        }

        var footer = document.createElement('div');
        footer.className = 'modal-footer';
        footer.style.cssText = 'display:flex;gap:12px;justify-content:flex-end;margin-top:24px;';
        if (actions && actions.length > 0) {
            actions.forEach(function(action) {
                footer.appendChild(createButton(action.label, action.variant, action.onClick));
            });
        } else {
            footer.appendChild(createButton('关闭', 'outline', function() { closeModal(); }));
        }

        modal.appendChild(header);
        modal.appendChild(body);
        modal.appendChild(footer);
        overlay.appendChild(modal);
        document.body.appendChild(overlay);

        requestAnimationFrame(function() {
            overlay.style.opacity = '1';
            modal.style.transform = 'scale(1)';
        });

        overlay.addEventListener('click', function(e) {
            if (e.target === overlay) closeModal();
        });

        function closeModal() {
            overlay.style.opacity = '0';
            modal.style.transform = 'scale(0.95)';
            setTimeout(function() { overlay.remove(); }, 250);
        }

        return { close: closeModal, element: overlay };
    }

    function showToast(message, type, duration) {
        type = type || 'info';
        duration = duration || 3000;

        var toast = document.createElement('div');
        toast.className = 'toast-notification';
        toast.style.cssText = 'position:fixed;bottom:24px;right:24px;padding:14px 24px;' +
            'background:' + (type === 'error' ? '#e17055' : type === 'success' ? '#00b894' :
             type === 'warning' ? '#fdcb6e' : '#6c5ce7') + ';color:#fff;border-radius:10px;' +
            'font-size:14px;font-weight:600;z-index:99999;box-shadow:0 8px 30px rgba(0,0,0,0.2);' +
            'transform:translateX(120%);transition:transform 0.35s cubic-bezier(0.68,-0.55,0.265,1.55);';
        toast.textContent = message;
        document.body.appendChild(toast);

        requestAnimationFrame(function() {
            toast.style.transform = 'translateX(0)';
        });

        setTimeout(function() {
            toast.style.transform = 'translateX(120%)';
            setTimeout(function() { toast.remove(); }, 350);
        }, duration);
    }

    function createProgressBar(maxValue) {
        var container = document.createElement('div');
        container.style.cssText = 'width:100%;height:8px;background:#e0e0e0;border-radius:4px;overflow:hidden;';

        var bar = document.createElement('div');
        bar.style.cssText = 'height:100%;background:linear-gradient(90deg,#6c5ce7,#00cec9);' +
            'border-radius:4px;width:0%;transition:width 0.35s ease;';
        container.appendChild(bar);

        return {
            element: container,
            setValue: function(val) {
                var pct = Math.min(100, Math.max(0, (val / maxValue) * 100));
                bar.style.width = pct.toFixed(1) + '%';
            },
            reset: function() { bar.style.width = '0%'; }
        };
    }

    function createDataTable(columns, rows) {
        var table = document.createElement('table');
        table.style.cssText = 'width:100%;border-collapse:collapse;font-size:14px;';

        var thead = document.createElement('thead');
        var headRow = document.createElement('tr');
        columns.forEach(function(col) {
            var th = document.createElement('th');
            th.textContent = col.label || col;
            th.style.cssText = 'padding:12px 16px;text-align:left;background:#f8f9fa;border-bottom:2px solid #dee2e6;font-weight:600;color:#495057;';
            headRow.appendChild(th);
        });
        thead.appendChild(headRow);
        table.appendChild(thead);

        var tbody = document.createElement('tbody');
        rows.forEach(function(row, idx) {
            var tr = document.createElement('tr');
            tr.style.cssText = idx % 2 ? 'background:#f8f9fa;' : '';
            columns.forEach(function(col) {
                var key = typeof col === 'object' ? col.key : col;
                var td = document.createElement('td');
                td.textContent = row[key] !== undefined ? row[key] : '';
                td.style.cssText = 'padding:12px 16px;border-bottom:1px solid #e9ecef;color:#6c757d;';
                tr.appendChild(td);
            });
            tbody.appendChild(tr);
        });
        table.appendChild(tbody);

        return table;
    }

    function createTabs(tabConfigs) {
        var wrapper = document.createElement('div');
        wrapper.className = 'tab-container';

        var tabBar = document.createElement('div');
        tabBar.style.cssText = 'display:flex;gap:4px;border-bottom:2px solid #e9ecef;margin-bottom:20px;';

        var panels = {};

        tabConfigs.forEach(function(config, idx) {
            var tabBtn = document.createElement('button');
            tabBtn.textContent = config.label;
            tabBtn.style.cssText = 'padding:10px 20px;border:none;background:none;cursor:pointer;' +
                'font-size:14px;font-weight:600;color:#6c757d;border-bottom:2px solid transparent;margin-bottom:-2px;' +
                'transition:all 0.2s;' + (idx === 0 ? 'color:#6c5ce7;border-bottom-color:#6c5ce7;' : '');
            tabBar.appendChild(tabBtn);

            var panel = document.createElement('div');
            panel.style.display = idx === 0 ? 'block' : 'none';
            if (config.content instanceof HTMLElement) {
                panel.appendChild(config.content);
            } else {
                panel.innerHTML = config.content || '';
            }
            panels[config.key] = panel;

            tabBtn.addEventListener('click', function() {
                Object.values(panels).forEach(function(p) { p.style.display = 'none'; });
                tabBar.querySelectorAll('button').forEach(function(b) {
                    b.style.color = '#6c757d'; b.style.borderColor = 'transparent';
                });
                panel.style.display = 'block';
                tabBtn.style.color = '#6c5ce7'; tabBtn.style.borderColor = '#6c5ce7';

                if (config.onSelect) config.onSelect(config.key);
            });
        });

        wrapper.appendChild(tabBar);
        tabConfigs.forEach(function(c) { wrapper.appendChild(panels[c.key]); });

        return wrapper;
    }

    return {
        createButton: createButton,
        createModal: createModal,
        showToast: showToast,
        createProgressBar: createProgressBar,
        createDataTable: createDataTable,
        createTabs: createTabs
    };
})();
