import { useState } from "react";
import axios from "axios";
import { motion, AnimatePresence } from "framer-motion";
import "./index.css"

const PAGE_SIZE = 10;

type Result = {
  title: string;
  url: string;
  snippet: string;
  score: number;
  pagerank: number;
};

// Custom Styles for Glassmorphism
const glassStyle = {
  background: "rgba(255, 255, 255, 0.1)",
  backdropFilter: "blur(12px)",
  WebkitBackdropFilter: "blur(12px)",
  border: "1px solid rgba(255, 255, 255, 0.2)",
  boxShadow: "0 8px 32px 0 rgba(31, 38, 135, 0.15)",
};

function App() {
  const [query, setQuery] = useState("");
  const [results, setResults] = useState<Result[]>([]);
  const [loading, setLoading] = useState(false);
  const [searched, setSearched] = useState(false);
  const [offset, setOffset] = useState(0);
  const [hasMore, setHasMore] = useState(false);

  const fetchResults = async (q: string, off: number, append: boolean) => {
    setLoading(true);
    try {
      const res = await axios.get(
        `http://localhost:8080/search?q=${encodeURIComponent(q)}&offset=${off}&limit=${PAGE_SIZE}`
      );
      const data: Result[] = res.data.results || [];
      setResults(prev => append ? [...prev, ...data] : data);
      setHasMore(data.length === PAGE_SIZE);
    } catch (err) {
      console.error("Search failed:", err);
    } finally {
      setLoading(false);
    }
  };

  const search = async () => {
    if (!query.trim()) return;
    setSearched(true);
    setOffset(0);
    await fetchResults(query, 0, false);
  };

  const loadMore = async () => {
    const next = offset + PAGE_SIZE;
    setOffset(next);
    await fetchResults(query, next, true);
  };

  return (
    <div style={{
      minHeight: "100vh",
      background: "linear-gradient(135deg, #0f172a 0%, #1e1b4b 50%, #312e81 100%)",
      color: "#f8fafc",
      padding: "40px 20px",
      fontFamily: "'Inter', system-ui, sans-serif"
    }}>
      <div style={{ maxWidth: "800px", margin: "0 auto" }}>
        
        {/* Header Section */}
        <header style={{ textAlign: "center", marginBottom: "60px" }}>
          <motion.h1 
            initial={{ opacity: 0, y: -20 }}
            animate={{ opacity: 1, y: 0 }}
            style={{
              fontSize: "4rem",
              fontWeight: "800",
              background: "linear-gradient(to right, #818cf8, #c084fc)",
              WebkitBackgroundClip: "text",
              WebkitTextFillColor: "transparent",
              margin: "0 0 10px 0",
              letterSpacing: "-0.05em"
            }}
          >
            WikiLens
          </motion.h1>
          <p style={{ color: "#94a3b8", fontSize: "1.1rem" }}>
            Precision search across documents via <span style={{ color: "#818cf8" }}>BM25 Ranking</span>
          </p>
        </header>

        {/* Search Bar Container */}
        <div style={{
          ...glassStyle,
          borderRadius: "24px",
          padding: "10px",
          display: "flex",
          gap: "10px",
          marginBottom: "50px",
          transition: "transform 0.2s ease"
        }}>
          <input
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && search()}
            placeholder="Search for anything..."
            style={{
              flex: 1,
              background: "transparent",
              border: "none",
              outline: "none",
              color: "white",
              padding: "15px 25px",
              fontSize: "1.1rem",
            }}
          />
          <button
            onClick={search}
            disabled={loading || !query.trim()}
            style={{
              background: "#6366f1",
              color: "white",
              border: "none",
              borderRadius: "16px",
              padding: "0 30px",
              fontWeight: "600",
              cursor: "pointer",
              transition: "all 0.3s ease",
              boxShadow: "0 4px 15px rgba(99, 102, 241, 0.4)"
            }}
          >
            {loading ? "..." : "Search"}
          </button>
        </div>

        {/* Results Area */}
        <div style={{ display: "flex", flexDirection: "column", gap: "20px" }}>
          <AnimatePresence>
            {results.map((result, index) => (
              <motion.div
                key={result.url}
                initial={{ opacity: 0, x: -20 }}
                animate={{ opacity: 1, x: 0 }}
                transition={{ delay: (index % PAGE_SIZE) * 0.05 }}
                whileHover={{ scale: 1.02, backgroundColor: "rgba(255, 255, 255, 0.15)" }}
                style={{
                  ...glassStyle,
                  borderRadius: "18px",
                  padding: "25px",
                }}
              >
                <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", gap: "12px" }}>
                  <h3 style={{ margin: 0, fontSize: "1.25rem" }}>
                    <a
                      href={result.url}
                      target="_blank"
                      rel="noopener noreferrer"
                      style={{ color: "#e2e8f0", textDecoration: "none" }}
                      onMouseOver={e => (e.currentTarget.style.color = "#818cf8")}
                      onMouseOut={e => (e.currentTarget.style.color = "#e2e8f0")}
                    >
                      {result.title}
                    </a>
                  </h3>
                  <div style={{
                    flexShrink: 0,
                    fontSize: "0.8rem",
                    fontWeight: "bold",
                    color: "#818cf8",
                    background: "rgba(129, 140, 248, 0.1)",
                    padding: "4px 12px",
                    borderRadius: "20px",
                    border: "1px solid rgba(129, 140, 248, 0.3)"
                  }}>
                    {result.score.toFixed(3)}
                  </div>
                </div>

                {result.snippet && (
                  <p style={{
                    margin: "10px 0 0 0",
                    color: "#94a3b8",
                    fontSize: "0.9rem",
                    lineHeight: "1.6",
                  }}>
                    {result.snippet}
                  </p>
                )}
              </motion.div>
            ))}
          </AnimatePresence>

          {/* Load More */}
          {hasMore && !loading && (
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              style={{ textAlign: "center", paddingTop: "10px" }}
            >
              <button
                onClick={loadMore}
                style={{
                  background: "rgba(99, 102, 241, 0.15)",
                  color: "#818cf8",
                  border: "1px solid rgba(129, 140, 248, 0.4)",
                  borderRadius: "12px",
                  padding: "12px 48px",
                  fontWeight: "600",
                  cursor: "pointer",
                  fontSize: "0.95rem",
                }}
              >
                Load more
              </button>
            </motion.div>
          )}

          {loading && results.length > 0 && (
            <p style={{ textAlign: "center", color: "#64748b" }}>Loading…</p>
          )}

          {/* Empty State */}
          {searched && !loading && results.length === 0 && (
            <motion.div 
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              style={{ textAlign: "center", padding: "40px", color: "#64748b" }}
            >
              <div style={{ fontSize: "3rem", marginBottom: "10px" }}>🔭</div>
              <p>No documents found matching "<strong>{query}</strong>"</p>
            </motion.div>
          )}
        </div>
      </div>
    </div>
  );
}

export default App;