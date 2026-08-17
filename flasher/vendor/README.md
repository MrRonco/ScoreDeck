esp-web-tools 10.4.0, bundled self-contained with esbuild (no runtime CDN
fetches, no dynamic imports) from the npm package. Regenerate:
  npm install esp-web-tools@10.4.0 esbuild
  echo 'import "esp-web-tools/dist/web/install-button.js";' > entry.js
  npx esbuild entry.js --bundle --format=esm --minify --outfile=esp-web-tools-10.4.0.min.js
