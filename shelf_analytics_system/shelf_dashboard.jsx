import { useState, useEffect, useCallback } from "react";

// ─── Simulated real-time data (replace fetch() with your API) ───
function generateMockEvent() {
  const types = ["customer_detected", "customer_interested", "item_pickup_detected",
                 "item_added_to_cart", "session_end"];
  const type = types[Math.floor(Math.random() * types.length)];
  const dwell = Math.floor(Math.random() * 20000) + 1000;
  return {
    event: type,
    ts: Date.now(),
    dwell_ms: dwell,
    interested: dwell > 5000,
    picked_up: Math.random() > 0.4,
    added_to_cart: Math.random() > 0.6,
    distance_cm: Math.floor(Math.random() * 60) + 20,
  };
}

const COLORS = {
  bg: "#0a0d14",
  surface: "#111827",
  border: "#1e2a3a",
  accent: "#00d4aa",
  accent2: "#3b82f6",
  accent3: "#f59e0b",
  accent4: "#ef4444",
  text: "#e2e8f0",
  muted: "#64748b",
  grid: "#1e293b",
};

const pct = (n, d) => d === 0 ? 0 : Math.round((n / d) * 100);

function StatCard({ label, value, sub, color = COLORS.accent, icon }) {
  return (
    <div style={{
      background: COLORS.surface,
      border: `1px solid ${COLORS.border}`,
      borderRadius: 12,
      padding: "20px 24px",
      borderLeft: `3px solid ${color}`,
      transition: "transform 0.2s",
    }}
      onMouseEnter={e => e.currentTarget.style.transform = "translateY(-2px)"}
      onMouseLeave={e => e.currentTarget.style.transform = "translateY(0)"}
    >
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
        <div>
          <div style={{ color: COLORS.muted, fontSize: 11, letterSpacing: "0.1em", textTransform: "uppercase", marginBottom: 6 }}>{label}</div>
          <div style={{ color, fontSize: 36, fontWeight: 800, fontFamily: "monospace", lineHeight: 1 }}>{value}</div>
          {sub && <div style={{ color: COLORS.muted, fontSize: 12, marginTop: 6 }}>{sub}</div>}
        </div>
        <div style={{ fontSize: 28, opacity: 0.7 }}>{icon}</div>
      </div>
    </div>
  );
}

function MiniBar({ label, value, max, color }) {
  const w = max > 0 ? (value / max) * 100 : 0;
  return (
    <div style={{ marginBottom: 14 }}>
      <div style={{ display: "flex", justifyContent: "space-between", marginBottom: 5 }}>
        <span style={{ color: COLORS.text, fontSize: 13 }}>{label}</span>
        <span style={{ color, fontWeight: 700, fontSize: 13, fontFamily: "monospace" }}>{value}</span>
      </div>
      <div style={{ background: COLORS.grid, borderRadius: 4, height: 6, overflow: "hidden" }}>
        <div style={{ width: `${w}%`, background: color, height: "100%", borderRadius: 4,
          transition: "width 0.5s cubic-bezier(0.4,0,0.2,1)" }} />
      </div>
    </div>
  );
}

function EventFeed({ events }) {
  const icons = {
    customer_detected: "👤",
    customer_interested: "👀",
    item_pickup_detected: "🤏",
    item_added_to_cart: "🛒",
    session_end: "✅",
    metrics_report: "📊",
  };
  const colors = {
    customer_detected: COLORS.accent2,
    customer_interested: COLORS.accent,
    item_pickup_detected: COLORS.accent3,
    item_added_to_cart: "#22c55e",
    session_end: COLORS.muted,
    metrics_report: "#a78bfa",
  };

  return (
    <div style={{
      background: COLORS.surface, border: `1px solid ${COLORS.border}`,
      borderRadius: 12, padding: 20, height: 320, overflowY: "auto"
    }}>
      <div style={{ color: COLORS.muted, fontSize: 11, letterSpacing: "0.1em", textTransform: "uppercase", marginBottom: 14 }}>
        Live Event Stream
      </div>
      {events.length === 0 && (
        <div style={{ color: COLORS.muted, textAlign: "center", paddingTop: 60 }}>
          Waiting for events...
        </div>
      )}
      {events.map((ev, i) => (
        <div key={i} style={{
          display: "flex", alignItems: "flex-start", gap: 10,
          padding: "8px 0",
          borderBottom: i < events.length - 1 ? `1px solid ${COLORS.border}` : "none",
          opacity: i === 0 ? 1 : Math.max(0.4, 1 - i * 0.08),
          animation: i === 0 ? "slideIn 0.3s ease" : "none",
        }}>
          <div style={{ fontSize: 18, flexShrink: 0, marginTop: 1 }}>{icons[ev.event] || "📡"}</div>
          <div style={{ flex: 1, minWidth: 0 }}>
            <div style={{ color: colors[ev.event] || COLORS.text, fontSize: 13, fontWeight: 600 }}>
              {ev.event.replace(/_/g, " ").replace(/\b\w/g, c => c.toUpperCase())}
            </div>
            <div style={{ color: COLORS.muted, fontSize: 11, marginTop: 2 }}>
              {ev.dwell_ms && `dwell ${(ev.dwell_ms / 1000).toFixed(1)}s`}
              {ev.distance_cm && ` · ${ev.distance_cm}cm`}
            </div>
          </div>
          <div style={{ color: COLORS.muted, fontSize: 10, fontFamily: "monospace", flexShrink: 0 }}>
            {new Date(ev.ts).toLocaleTimeString()}
          </div>
        </div>
      ))}
    </div>
  );
}

function FunnelChart({ footfall, interested, pickups, cart }) {
  const stages = [
    { label: "Footfall", value: footfall, color: COLORS.accent2 },
    { label: "Interested (>5s)", value: interested, color: COLORS.accent },
    { label: "Picked Up", value: pickups, color: COLORS.accent3 },
    { label: "Added to Cart", value: cart, color: "#22c55e" },
  ];
  const maxW = 280;
  return (
    <div style={{
      background: COLORS.surface, border: `1px solid ${COLORS.border}`,
      borderRadius: 12, padding: 24
    }}>
      <div style={{ color: COLORS.muted, fontSize: 11, letterSpacing: "0.1em", textTransform: "uppercase", marginBottom: 20 }}>
        Conversion Funnel
      </div>
      <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 8 }}>
        {stages.map((s, i) => {
          const w = footfall > 0 ? Math.max(30, (s.value / footfall) * maxW) : 30;
          return (
            <div key={i} style={{ display: "flex", alignItems: "center", gap: 12, width: "100%" }}>
              <div style={{ width: 100, textAlign: "right", color: COLORS.muted, fontSize: 12 }}>{s.label}</div>
              <div style={{ flex: 1, display: "flex", alignItems: "center", gap: 8 }}>
                <div style={{
                  width: w, height: 32, background: s.color,
                  borderRadius: 4, opacity: 0.85,
                  transition: "width 0.6s cubic-bezier(0.4,0,0.2,1)",
                  display: "flex", alignItems: "center", paddingLeft: 8
                }}>
                  <span style={{ color: "#000", fontWeight: 700, fontSize: 13 }}>{s.value}</span>
                </div>
                <span style={{ color: COLORS.muted, fontSize: 11 }}>
                  {footfall > 0 && i > 0 ? `${pct(s.value, footfall)}%` : ""}
                </span>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}

export default function ShelfDashboard() {
  const [metrics, setMetrics] = useState({
    footfall: 0, interested: 0, pickups: 0, cart: 0,
    totalDwellMs: 0, sessions: 0
  });
  const [events, setEvents] = useState([]);
  const [isLive, setIsLive] = useState(true);

  const processEvent = useCallback((ev) => {
    setEvents(prev => [ev, ...prev].slice(0, 50));
    setMetrics(prev => {
      const next = { ...prev };
      if (ev.event === "customer_detected") next.footfall++;
      if (ev.event === "customer_interested") next.interested++;
      if (ev.event === "item_pickup_detected") next.pickups++;
      if (ev.event === "item_added_to_cart") next.cart++;
      if (ev.event === "session_end") {
        next.sessions++;
        next.totalDwellMs += ev.dwell_ms || 0;
        if (ev.interested && ev.event !== "customer_interested") {
          // already counted
        }
      }
      return next;
    });
  }, []);

  // ── Simulate events (replace with real WebSocket or polling) ──
  useEffect(() => {
    if (!isLive) return;
    const interval = setInterval(() => {
      processEvent(generateMockEvent());
    }, 2000 + Math.random() * 2000);
    return () => clearInterval(interval);
  }, [isLive, processEvent]);

  // ── To connect real device: poll your Flask/Node server ──
  // useEffect(() => {
  //   const poll = setInterval(async () => {
  //     const r = await fetch("http://YOUR_SERVER/api/events/latest");
  //     const ev = await r.json();
  //     processEvent(ev);
  //   }, 1000);
  //   return () => clearInterval(poll);
  // }, [processEvent]);

  const avgDwell = metrics.sessions > 0
    ? (metrics.totalDwellMs / metrics.sessions / 1000).toFixed(1) : "0.0";

  return (
    <div style={{
      minHeight: "100vh", background: COLORS.bg, color: COLORS.text,
      fontFamily: "'Segoe UI', system-ui, sans-serif", padding: "24px 28px",
    }}>
      <style>{`
        @keyframes slideIn { from { opacity:0; transform:translateY(-8px); } to { opacity:1; transform:none; } }
        @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:0.4} }
        ::-webkit-scrollbar { width:4px; } ::-webkit-scrollbar-track { background:transparent; }
        ::-webkit-scrollbar-thumb { background:${COLORS.border}; border-radius:2px; }
      `}</style>

      {/* Header */}
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 28 }}>
        <div>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <span style={{ fontSize: 22 }}>🛍️</span>
            <h1 style={{ margin: 0, fontSize: 22, fontWeight: 800, color: COLORS.text }}>
              Shelf Analytics
            </h1>
            <span style={{ background: "#1e3a5f", color: COLORS.accent2, fontSize: 10,
              padding: "2px 8px", borderRadius: 20, fontWeight: 600, letterSpacing: "0.05em" }}>
              SHELF-01
            </span>
          </div>
          <div style={{ color: COLORS.muted, fontSize: 12, marginTop: 4 }}>
            Real-time retail intelligence · Arduino Uno / ESP32-CAM
          </div>
        </div>
        <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
          <div style={{
            width: 8, height: 8, borderRadius: "50%",
            background: isLive ? "#22c55e" : COLORS.muted,
            animation: isLive ? "pulse 1.5s infinite" : "none"
          }} />
          <span style={{ fontSize: 12, color: isLive ? "#22c55e" : COLORS.muted }}>
            {isLive ? "LIVE" : "PAUSED"}
          </span>
          <button onClick={() => setIsLive(p => !p)} style={{
            background: COLORS.surface, border: `1px solid ${COLORS.border}`,
            color: COLORS.text, borderRadius: 8, padding: "6px 14px",
            cursor: "pointer", fontSize: 12, fontWeight: 600
          }}>
            {isLive ? "⏸ Pause" : "▶ Resume"}
          </button>
          <button onClick={() => { setMetrics({footfall:0,interested:0,pickups:0,cart:0,totalDwellMs:0,sessions:0}); setEvents([]); }}
            style={{
              background: "transparent", border: `1px solid ${COLORS.border}`,
              color: COLORS.muted, borderRadius: 8, padding: "6px 14px",
              cursor: "pointer", fontSize: 12
            }}>
            Reset
          </button>
        </div>
      </div>

      {/* KPI Row */}
      <div style={{ display: "grid", gridTemplateColumns: "repeat(4,1fr)", gap: 14, marginBottom: 20 }}>
        <StatCard label="Footfall"        value={metrics.footfall}   icon="👤" color={COLORS.accent2} sub="Total customers detected" />
        <StatCard label="Interested"      value={metrics.interested} icon="👀" color={COLORS.accent}  sub={`${pct(metrics.interested, metrics.footfall)}% of footfall (>5s dwell)`} />
        <StatCard label="Items Picked Up" value={metrics.pickups}    icon="🤏" color={COLORS.accent3} sub={`${pct(metrics.pickups, metrics.footfall)}% pickup rate`} />
        <StatCard label="Added to Cart"   value={metrics.cart}       icon="🛒" color="#22c55e"        sub={`${pct(metrics.cart, metrics.footfall)}% conversion`} />
      </div>

      {/* Row 2 */}
      <div style={{ display: "grid", gridTemplateColumns: "repeat(3,1fr)", gap: 14, marginBottom: 20 }}>
        <StatCard label="Avg Dwell Time" value={`${avgDwell}s`} icon="⏱️" color="#a78bfa" sub={`${metrics.sessions} sessions recorded`} />
        <StatCard label="Interest Rate"  value={`${pct(metrics.interested, metrics.footfall)}%`} icon="📈" color={COLORS.accent} sub="Customers spending >5s" />
        <StatCard label="Cart Abandon"   value={`${pct(metrics.pickups - metrics.cart, metrics.footfall)}%`} icon="⚠️" color={COLORS.accent4} sub="Picked up but not carted" />
      </div>

      {/* Funnel + Bars + Feed */}
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gap: 14 }}>
        <FunnelChart footfall={metrics.footfall} interested={metrics.interested} pickups={metrics.pickups} cart={metrics.cart} />

        <div style={{ background: COLORS.surface, border: `1px solid ${COLORS.border}`, borderRadius: 12, padding: 24 }}>
          <div style={{ color: COLORS.muted, fontSize: 11, letterSpacing: "0.1em", textTransform: "uppercase", marginBottom: 20 }}>
            Metric Breakdown
          </div>
          <MiniBar label="Footfall"       value={metrics.footfall}   max={Math.max(metrics.footfall, 1)} color={COLORS.accent2} />
          <MiniBar label="Interested"     value={metrics.interested} max={Math.max(metrics.footfall, 1)} color={COLORS.accent} />
          <MiniBar label="Pickups"        value={metrics.pickups}    max={Math.max(metrics.footfall, 1)} color={COLORS.accent3} />
          <MiniBar label="Added to Cart"  value={metrics.cart}       max={Math.max(metrics.footfall, 1)} color="#22c55e" />
          <div style={{ marginTop: 20, paddingTop: 16, borderTop: `1px solid ${COLORS.border}` }}>
            <div style={{ color: COLORS.muted, fontSize: 11, marginBottom: 10 }}>DETECTION METHOD</div>
            <div style={{ display: "flex", gap: 8, flexWrap: "wrap" }}>
              {["PIR Motion", "Ultrasonic", "Frame Diff", "Vibration"].map(m => (
                <span key={m} style={{ background: "#1e2a3a", color: COLORS.accent, fontSize: 10,
                  padding: "3px 8px", borderRadius: 12, fontWeight: 600 }}>{m}</span>
              ))}
            </div>
          </div>
        </div>

        <EventFeed events={events} />
      </div>

      {/* Footer note */}
      <div style={{ marginTop: 20, padding: "12px 16px", background: COLORS.surface,
        border: `1px solid ${COLORS.border}`, borderRadius: 8,
        display: "flex", alignItems: "center", gap: 10 }}>
        <span style={{ fontSize: 16 }}>💡</span>
        <span style={{ color: COLORS.muted, fontSize: 12 }}>
          <strong style={{ color: COLORS.text }}>Live mode</strong> is simulating events. 
          To connect real hardware: uncomment the polling block in the source and point it to your Flask/Node server receiving ESP32-CAM REST POSTs.
          Arduino Uno outputs newline-delimited JSON at 115200 baud — parse with a serial bridge script.
        </span>
      </div>
    </div>
  );
}
