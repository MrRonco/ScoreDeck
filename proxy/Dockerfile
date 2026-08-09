# Self-host image: Unraid, a NAS, a Pi, any box the panel can reach.
FROM node:22-alpine

# Run unprivileged. The image needs no write access to anything.
WORKDIR /app
COPY package.json ./
RUN npm install --omit=dev && npm cache clean --force
COPY tsconfig.json ./
COPY src ./src
USER node

ENV PORT=8787 NODE_ENV=production
EXPOSE 8787

# Unraid shows this as the container health dot.
HEALTHCHECK --interval=60s --timeout=5s --start-period=10s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:'+(process.env.PORT||8787)+'/v1/health',{headers:process.env.SD_TOKEN?{authorization:'Bearer '+process.env.SD_TOKEN}:{}}).then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"

CMD ["node", "--experimental-strip-types", "src/server.ts"]
