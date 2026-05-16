FROM node:18-bookworm-slim

WORKDIR /app

COPY package*.json ./
RUN npm install --omit=dev

RUN apt-get update \
    && apt-get install -y --no-install-recommends python3 python3-pip python3-venv \
    && rm -rf /var/lib/apt/lists/*

COPY . .
RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/python -m pip install --upgrade pip \
    && /opt/venv/bin/python -m pip install --no-cache-dir --index-url https://download.pytorch.org/whl/cpu torch==2.11.0 \
    && /opt/venv/bin/python -m pip install --no-cache-dir -r sequence_backend/requirements.txt

ENV PATH="/opt/venv/bin:${PATH}"

ENV PORT=80

EXPOSE 80

CMD ["npm", "start"]
