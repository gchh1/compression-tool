// WebCompress Pro - API Client Module
var API = (function() {
    var BASE_URL = '/api/v1';

    function request(endpoint, options) {
        options = options || {};
        options.headers = options.headers || {};
        options.headers['Content-Type'] = 'application/json';

        return fetch(BASE_URL + endpoint, options)
            .then(function(res) {
                if (!res.ok) throw new Error('API Error: ' + res.status);
                return res.json();
            })
            .catch(function(err) {
                console.error('API Request Failed:', err);
                throw err;
            });
    }

    function compressFile(fileData, algorithm, quality) {
        return request('/compress', {
            method: 'POST',
            body: JSON.stringify({
                data: fileData,
                algorithm: algorithm || 'deflate',
                quality: quality || 85
            })
        });
    }

    function decompressData(compressedData) {
        return request('/decompress', {
            method: 'POST',
            body: JSON.stringify({ data: compressedData })
        });
    }

    function getCompressionStats(fileId) {
        return request('/stats/' + fileId);
    }

    function getBenchmarkResults(algorithm) {
        return request('/benchmark?algorithm=' + (algorithm || 'all'));
    }

    function uploadFiles(files) {
        var formData = new FormData();
        for (var i = 0; i < files.length; i++) {
            formData.append('files', files[i]);
        }
        return fetch(BASE_URL + '/upload', { method: 'POST', body: formData })
            .then(function(r) { return r.json(); });
    }

    return {
        compressFile: compressFile,
        decompressData: decompressData,
        getCompressionStats: getCompressionStats,
        getBenchmarkResults: getBenchmarkResults,
        uploadFiles: uploadFiles
    };
});
