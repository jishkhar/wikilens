import { useEffect, useRef, useState } from "react";
import axios from "axios";
import { AnimatePresence, motion } from "framer-motion";
import "./index.css";

const PAGE_SIZE = 10;
const SUGGEST_LIMIT = 8;
const API_BASE_URL = (import.meta.env.VITE_API_BASE_URL || "/api").replace(/\/$/, "");

type Result = {
  title: string;
  url: string;
  snippet: string;
  score: number;
  pagerank: number;
};

type SearchResponse = {
  results?: Result[];
};

type SuggestResponse = {
  suggestions?: string[];
};

const glassStyle = {
  background: "rgba(255, 255, 255, 0.1)",
  backdropFilter: "blur(12px)",
  WebkitBackdropFilter: "blur(12px)",
  border: "1px solid rgba(255, 255, 255, 0.2)",
  boxShadow: "0 8px 32px 0 rgba(31, 38, 135, 0.15)",
};

function isEditableTarget(target: EventTarget | null) {
  if (!(target instanceof HTMLElement)) {
    return false;
  }

  const tagName = target.tagName;
  return (
    target.isContentEditable ||
    tagName === "INPUT" ||
    tagName === "TEXTAREA" ||
    tagName === "SELECT"
  );
}

function extractActiveToken(query: string) {
  let end = query.length;
  while (end > 0 && !/[A-Za-z0-9]/.test(query[end - 1])) {
    end -= 1;
  }

  let start = end;
  while (start > 0 && /[A-Za-z0-9]/.test(query[start - 1])) {
    start -= 1;
  }

  return { start, end, token: query.slice(start, end) };
}

function replaceActiveToken(query: string, suggestion: string) {
  const { start, end } = extractActiveToken(query);
  return query.slice(0, start) + suggestion + query.slice(end);
}

function App() {
  const [query, setQuery] = useState("");
  const [results, setResults] = useState<Result[]>([]);
  const [loading, setLoading] = useState(false);
  const [searched, setSearched] = useState(false);
  const [offset, setOffset] = useState(0);
  const [hasMore, setHasMore] = useState(false);
  const [errorMessage, setErrorMessage] = useState("");
  const [suggestions, setSuggestions] = useState<string[]>([]);
  const [loadingSuggestions, setLoadingSuggestions] = useState(false);
  const [showSuggestions, setShowSuggestions] = useState(false);
  const inputRef = useRef<HTMLInputElement | null>(null);
  const searchBoxRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    const handleKeyDown = (event: KeyboardEvent) => {
      if (
        event.key !== "/" ||
        event.metaKey ||
        event.ctrlKey ||
        event.altKey ||
        event.shiftKey ||
        isEditableTarget(event.target)
      ) {
        return;
      }

      event.preventDefault();
      inputRef.current?.focus();
      inputRef.current?.select();
    };

    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, []);

  useEffect(() => {
    const handlePointerDown = (event: MouseEvent) => {
      if (!searchBoxRef.current?.contains(event.target as Node)) {
        setShowSuggestions(false);
      }
    };

    window.addEventListener("mousedown", handlePointerDown);
    return () => window.removeEventListener("mousedown", handlePointerDown);
  }, []);

  useEffect(() => {
    const trimmed = query.trim();
    const { token } = extractActiveToken(query);

    if (!trimmed || !token) {
      setSuggestions([]);
      setLoadingSuggestions(false);
      return;
    }

    const timer = window.setTimeout(async () => {
      setLoadingSuggestions(true);

      try {
        const res = await axios.get<SuggestResponse>(`${API_BASE_URL}/suggest`, {
          params: {
            q: query,
            limit: SUGGEST_LIMIT,
          },
        });

        const nextSuggestions = res.data.suggestions || [];
        setSuggestions(nextSuggestions);
        setShowSuggestions(document.activeElement === inputRef.current && nextSuggestions.length > 0);
      } catch (error) {
        console.error("Suggest failed:", error);
        setSuggestions([]);
      } finally {
        setLoadingSuggestions(false);
      }
    }, 300);

    return () => window.clearTimeout(timer);
  }, [query]);

  const fetchResults = async (q: string, off: number, append: boolean) => {
    setLoading(true);
    setErrorMessage("");
    setShowSuggestions(false);

    try {
      const res = await axios.get<SearchResponse>(`${API_BASE_URL}/search`, {
        params: {
          q,
          offset: off,
          limit: PAGE_SIZE,
        },
      });

      const data: Result[] = res.data.results || [];
      setResults((prev) => (append ? [...prev, ...data] : data));
      setHasMore(data.length === PAGE_SIZE);
      return true;
    } catch (error) {
      setHasMore(false);
      if (!append) {
        setResults([]);
      }

      if (axios.isAxiosError(error)) {
        if (typeof error.response?.data === "string" && error.response.data.trim()) {
          setErrorMessage(error.response.data);
        } else if (!error.response) {
          setErrorMessage(
            "Could not reach the WikiLens backend. Start the API or set VITE_API_BASE_URL for this frontend."
          );
        } else {
          setErrorMessage("Search failed. Please try again in a moment.");
        }
      } else {
        setErrorMessage("Search failed unexpectedly. Please try again.");
      }

      console.error("Search failed:", error);
      return false;
    } finally {
      setLoading(false);
    }
  };

  const runSearch = async (nextQuery: string) => {
    const trimmed = nextQuery.trim();
    if (!trimmed) return;

    setSearched(true);
    const ok = await fetchResults(trimmed, 0, false);
    if (ok) {
      setOffset(0);
    }
  };

  const search = async () => {
    await runSearch(query);
  };

  const loadMore = async () => {
    const next = offset + PAGE_SIZE;
    const ok = await fetchResults(query.trim(), next, true);
    if (ok) {
      setOffset(next);
    }
  };

  const applySuggestion = async (suggestion: string) => {
    const nextQuery = replaceActiveToken(query, suggestion);
    setQuery(nextQuery);
    setSuggestions([]);
    setShowSuggestions(false);
    await runSearch(nextQuery);
  };

  const showInitialLoading = loading && results.length === 0;
  const showLoadMoreLoading = loading && results.length > 0;
  const showSuggestionMenu = showSuggestions && suggestions.length > 0;

  return (
    <div
      style={{
        minHeight: "100vh",
        background: "linear-gradient(135deg, #0f172a 0%, #1e1b4b 50%, #312e81 100%)",
        color: "#f8fafc",
        padding: "clamp(24px, 5vw, 40px) clamp(14px, 4vw, 20px)",
        fontFamily: "'Inter', system-ui, sans-serif",
      }}
    >
      <div style={{ maxWidth: "800px", margin: "0 auto" }}>
        <header style={{ textAlign: "center", marginBottom: "clamp(32px, 8vw, 60px)" }}>
          <motion.h1
            initial={{ opacity: 0, y: -20 }}
            animate={{ opacity: 1, y: 0 }}
            style={{
              fontSize: "clamp(2.7rem, 11vw, 4rem)",
              fontWeight: "800",
              background: "linear-gradient(to right, #818cf8, #c084fc)",
              WebkitBackgroundClip: "text",
              WebkitTextFillColor: "transparent",
              margin: "0 0 10px 0",
              letterSpacing: "-0.05em",
              lineHeight: 0.95,
            }}
          >
            WikiLens
          </motion.h1>
          <p style={{ color: "#94a3b8", fontSize: "clamp(1rem, 3.5vw, 1.1rem)", lineHeight: 1.6 }}>
            Precision search across documents via <span style={{ color: "#818cf8" }}>BM25 Ranking</span>
          </p>
          <p style={{ color: "#64748b", fontSize: "0.95rem", marginTop: "10px" }}>
            Press <code>/</code> to focus search
          </p>
        </header>

        <div
          ref={searchBoxRef}
          style={{
            ...glassStyle,
            borderRadius: "24px",
            padding: "10px",
            display: "flex",
            gap: "10px",
            marginBottom: "24px",
            transition: "transform 0.2s ease",
            flexWrap: "wrap",
            position: "relative",
          }}
        >
          <div style={{ flex: "1 1 260px", minWidth: 0, position: "relative" }}>
            <input
              ref={inputRef}
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              onFocus={() => {
                if (suggestions.length > 0) {
                  setShowSuggestions(true);
                }
              }}
              onKeyDown={(e) => {
                if (e.key === "Enter") {
                  e.preventDefault();
                  void search();
                }

                if (e.key === "Escape") {
                  setShowSuggestions(false);
                }
              }}
              placeholder="Search for anything..."
              style={{
                width: "100%",
                minWidth: 0,
                background: "transparent",
                border: "none",
                outline: "none",
                color: "white",
                padding: "15px 20px",
                fontSize: "1.05rem",
              }}
            />

            {showSuggestionMenu && (
              <div
                style={{
                  position: "absolute",
                  top: "calc(100% + 8px)",
                  left: 0,
                  right: 0,
                  zIndex: 20,
                  ...glassStyle,
                  borderRadius: "18px",
                  overflow: "hidden",
                  background: "rgba(15, 23, 42, 0.94)",
                }}
              >
                {suggestions.map((suggestion, index) => (
                  <button
                    key={`${suggestion}-${index}`}
                    onMouseDown={(e) => e.preventDefault()}
                    onClick={() => void applySuggestion(suggestion)}
                    style={{
                      width: "100%",
                      background: "transparent",
                      border: "none",
                      borderBottom:
                        index === suggestions.length - 1
                          ? "none"
                          : "1px solid rgba(148, 163, 184, 0.12)",
                      color: "#e2e8f0",
                      textAlign: "left",
                      padding: "12px 18px",
                      cursor: "pointer",
                    }}
                  >
                    {suggestion}
                  </button>
                ))}
              </div>
            )}
          </div>

          <button
            onClick={() => void search()}
            disabled={loading || !query.trim()}
            style={{
              background: "#6366f1",
              color: "white",
              border: "none",
              borderRadius: "16px",
              padding: "0 24px",
              minHeight: "56px",
              flex: "1 1 160px",
              fontWeight: "600",
              cursor: loading || !query.trim() ? "not-allowed" : "pointer",
              transition: "all 0.3s ease",
              boxShadow: "0 4px 15px rgba(99, 102, 241, 0.4)",
              opacity: loading || !query.trim() ? 0.7 : 1,
              display: "inline-flex",
              alignItems: "center",
              justifyContent: "center",
              gap: "10px",
            }}
          >
            {(loading || loadingSuggestions) && (
              <span className="spinner spinner-small" aria-hidden="true" />
            )}
            <span>{loading ? "Searching" : "Search"}</span>
          </button>
        </div>

        {errorMessage && (
          <motion.div
            initial={{ opacity: 0, y: -8 }}
            animate={{ opacity: 1, y: 0 }}
            style={{
              ...glassStyle,
              borderRadius: "18px",
              padding: "18px 20px",
              marginBottom: "24px",
              border: "1px solid rgba(248, 113, 113, 0.35)",
              background: "rgba(127, 29, 29, 0.28)",
            }}
          >
            <p style={{ color: "#fecaca", fontWeight: 600, marginBottom: "6px" }}>
              Search unavailable
            </p>
            <p style={{ color: "#fee2e2", lineHeight: 1.6 }}>{errorMessage}</p>
          </motion.div>
        )}

        <div style={{ display: "flex", flexDirection: "column", gap: "clamp(16px, 4vw, 20px)" }}>
          {showInitialLoading && (
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              style={{
                ...glassStyle,
                borderRadius: "20px",
                padding: "clamp(28px, 7vw, 40px) clamp(18px, 5vw, 24px)",
                display: "flex",
                flexDirection: "column",
                alignItems: "center",
                gap: "14px",
              }}
            >
              <span className="spinner" aria-hidden="true" />
              <p style={{ color: "#cbd5e1", fontSize: "1rem" }}>
                Searching the index…
              </p>
              <p style={{ color: "#64748b", fontSize: "0.95rem" }}>
                Pulling the best matches for “{query.trim()}”
              </p>
            </motion.div>
          )}

          <AnimatePresence>
            {results.map((result, index) => (
              <motion.div
                key={`${result.url}-${index}`}
                initial={{ opacity: 0, x: -20 }}
                animate={{ opacity: 1, x: 0 }}
                transition={{ delay: (index % PAGE_SIZE) * 0.05 }}
                whileHover={{ scale: 1.02, backgroundColor: "rgba(255, 255, 255, 0.15)" }}
                style={{
                  ...glassStyle,
                  borderRadius: "18px",
                  padding: "clamp(18px, 4vw, 25px)",
                }}
              >
                <div
                  style={{
                    display: "flex",
                    justifyContent: "space-between",
                    alignItems: "flex-start",
                    gap: "12px",
                    flexWrap: "wrap",
                  }}
                >
                  <h3 style={{ margin: 0, fontSize: "1.25rem", flex: "1 1 280px" }}>
                    <a
                      href={result.url}
                      target="_blank"
                      rel="noopener noreferrer"
                      style={{ color: "#e2e8f0", textDecoration: "none" }}
                      onMouseOver={(e) => (e.currentTarget.style.color = "#818cf8")}
                      onMouseOut={(e) => (e.currentTarget.style.color = "#e2e8f0")}
                    >
                      {result.title}
                    </a>
                  </h3>
                  <div
                    style={{
                      flexShrink: 0,
                      fontSize: "0.8rem",
                      fontWeight: "bold",
                      color: "#818cf8",
                      background: "rgba(129, 140, 248, 0.1)",
                      padding: "4px 12px",
                      borderRadius: "20px",
                      border: "1px solid rgba(129, 140, 248, 0.3)",
                    }}
                  >
                    {result.score.toFixed(3)}
                  </div>
                </div>

                {result.snippet && (
                  <p
                    style={{
                      margin: "10px 0 0 0",
                      color: "#94a3b8",
                      fontSize: "0.9rem",
                      lineHeight: "1.6",
                    }}
                  >
                    {result.snippet}
                  </p>
                )}
              </motion.div>
            ))}
          </AnimatePresence>

          {hasMore && !showLoadMoreLoading && (
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              style={{ textAlign: "center", paddingTop: "6px" }}
            >
              <button
                onClick={() => void loadMore()}
                style={{
                  background: "rgba(99, 102, 241, 0.15)",
                  color: "#818cf8",
                  border: "1px solid rgba(129, 140, 248, 0.4)",
                  borderRadius: "12px",
                  padding: "12px clamp(24px, 8vw, 48px)",
                  fontWeight: "600",
                  cursor: "pointer",
                  fontSize: "0.95rem",
                }}
              >
                Load more
              </button>
            </motion.div>
          )}

          {showLoadMoreLoading && (
            <div style={{ textAlign: "center", color: "#94a3b8", paddingTop: "8px" }}>
              <span className="spinner spinner-small" aria-hidden="true" /> Loading more results…
            </div>
          )}

          {searched && !loading && !errorMessage && results.length === 0 && (
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              style={{ textAlign: "center", padding: "40px", color: "#64748b" }}
            >
              <div style={{ fontSize: "3rem", marginBottom: "10px" }}>🔭</div>
              <p>
                No documents found matching "<strong>{query.trim()}</strong>"
              </p>
            </motion.div>
          )}
        </div>
      </div>
    </div>
  );
}

export default App;
