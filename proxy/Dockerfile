# Self-host path: a Pi, a NAS, or any box the panel can reach.
FROM node:22-alpine
WORKDIR /app
COPY package.json ./
RUN npm install --omit=dev
COPY tsconfig.json ./
COPY src ./src
ENV PORT=8787
EXPOSE 8787
# SD_TOKEN is required in production; the app runs open without it.
CMD ["node", "--experimental-strip-types", "src/server.ts"]
