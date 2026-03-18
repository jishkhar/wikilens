# Frontend

WikiLens uses a React 19 + TypeScript + Vite frontend for the search UI.

## What It Does

- Calls the backend search API through `/api`
- Renders title, snippet, score, and link for each result
- Supports load-more pagination
- Shows loading and backend-unreachable states
- Supports `/` as a keyboard shortcut to focus the search box

## Development

Install dependencies and start the dev server:

```bash
cd frontend
npm install
npm run dev
```

Open `http://localhost:5173`.

## Backend API Routing

During local development, Vite proxies `/api/*` to `http://localhost:8080`.
That proxy is configured in [vite.config.ts](/home/nyx/Projects/wikilens/frontend/vite.config.ts).

The app also supports an explicit backend base URL through `VITE_API_BASE_URL`.

Example:

```bash
cd frontend
VITE_API_BASE_URL=http://localhost:8080 npm run dev
```

If `VITE_API_BASE_URL` is not set, the frontend defaults to `/api`.

## Build

Create a production build with:

```bash
cd frontend
npm run build
```

Preview the production bundle locally:

```bash
cd frontend
npm run preview
```

## Notes

- The favicon is stored at `frontend/public/favicon.svg`.
- The page title is set in `frontend/index.html`.
- For container or reverse-proxy deployment, keep `/api` routed to the backend service.
