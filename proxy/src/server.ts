// Local dev / self-host entry. `npm run dev`
import { createApp, defaultEnv } from './app.ts';

const app = createApp({ ...defaultEnv(), token: process.env.SD_TOKEN });
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
