const txtPreview = document.getElementById("txt-preview");
const metrics = document.getElementById("metrics");
const loadBtn = document.getElementById("load-products");

async function loadText() {
  const res = await fetch("./assets/text/sample.txt");
  const text = await res.text();
  txtPreview.textContent = text.slice(0, 280) + "...";
}

async function loadProducts() {
  const t0 = performance.now();
  const res = await fetch("./assets/data/products.json");
  const data = await res.json();
  const t1 = performance.now();

  const sizes = data.products.map((x) => x.sizeBytes);
  const origin = sizes.reduce((a, b) => a + b, 0);
  const compressed = data.products.reduce((acc, item) => acc + item.estimateCompressedBytes, 0);
  const ratio = ((origin - compressed) / origin * 100).toFixed(2);

  metrics.textContent = [
    `Products loaded: ${data.products.length}`,
    `Source bytes: ${origin}`,
    `Estimated compressed bytes: ${compressed}`,
    `Estimated ratio: ${ratio}%`,
    `Fetch+parse time: ${(t1 - t0).toFixed(2)} ms`
  ].join("\n");
}

loadBtn?.addEventListener("click", loadProducts);
loadText();
