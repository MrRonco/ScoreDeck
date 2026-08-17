// Local dev / self-host entry. `npm run dev`
import { createApp, defaultEnv } from './app.ts';

import { readFile } from 'node:fs/promises';
import { join, dirname, normalize, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

// Repo layout puts assets two up from src/; a container mounts them wherever.
const ASSETS = process.env.SD_ASSETS
  ?? join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'assets');

/** Read a built asset. Path is validated by the route; normalise anyway. */
async function assets(rel: string): Promise<ArrayBuffer | undefined> {
  const full = join(ASSETS, normalize(rel).replace(/^(\.\.[/\\])+/, ''));
  if (full !== ASSETS && !full.startsWith(ASSETS + sep)) return undefined;
  try {
    const b = await readFile(full);
    return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength) as ArrayBuffer;
  } catch {
    return undefined;
  }
}

const app = createApp({
  ...defaultEnv(),
  token: process.env.SD_TOKEN,
  allowAnonymous: process.env.SD_ALLOW_ANONYMOUS === '1',
  assets,
});
if (!process.env.SD_TOKEN && process.env.SD_ALLOW_ANONYMOUS !== '1')
  console.warn('scoredeck-proxy: SD_TOKEN unset — refusing all requests (set SD_ALLOW_ANONYMOUS=1 to override)');
const port = Number(process.env.PORT ?? 8787);

const server = (await import('node:http')).createServer(async (req, res) => {
  const url = `http://${req.headers.host ?? 'localhost'}${req.url}`;
  const body = ['GET', 'HEAD'].includes(req.method ?? 'GET') ? undefined : req;
  const r = await app.fetch(
    new Request(url, { method: req.method, headers: req.headers as any, body: body as any, duplex: 'half' } as any),
  );
  res.writeHead(r.status, Object.fromEntries(r.headers));
  res.end(Buffer.from(await r.arrayBuffer()));
});
server.listen(port, '0.0.0.0', () =>
  console.log(`scoredeck-proxy on http://0.0.0.0:${port}  token=${process.env.SD_TOKEN ? 'set' : 'none'}`));
