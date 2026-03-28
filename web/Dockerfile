FROM node:18-alpine AS builder

WORKDIR /web

COPY package.json package-lock.json* ./

RUN npm install

COPY . .

RUN npm run build

FROM node:18-alpine

WORKDIR /web

RUN npm install -g serve

COPY --from=builder /web/build ./build

EXPOSE 3000

CMD ["serve", "-s", "build", "-l", "3000"]