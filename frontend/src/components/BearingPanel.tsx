'use client';

import { useMemo } from 'react';
import {
  ComposedChart,
  Line,
  Area,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
} from 'recharts';
import { useAnchorObservations, useLatestBearing } from '@/store/session';
import { formatUptimeMMSS } from '@/lib/format';

/**
 * Interpolate the needle / accent color by confidence.
 * Full teal (#00d4aa) at 1.0 → muted gray (#6b7280) at 0.0.
 */
function confidenceColor(confidence: number): string {
  const c = Math.max(0, Math.min(1, confidence));
  const teal = [0, 212, 170];
  const gray = [107, 114, 128];
  const ch = teal.map((t, i) => Math.round(gray[i] + (t - gray[i]) * c));
  return `rgb(${ch[0]}, ${ch[1]}, ${ch[2]})`;
}

/**
 * Compass rose SVG with a needle rotated to the current bearing.
 * 0° points up; angle increases clockwise.
 */
function CompassRose({
  bearingDeg,
  confidence,
}: {
  bearingDeg: number | null;
  confidence: number;
}) {
  const size = 160;
  const cx = size / 2;
  const cy = size / 2;
  const r = size / 2 - 16;
  const color = confidenceColor(confidence);

  // Needle endpoint: 0° up, clockwise positive.
  const rad = ((bearingDeg ?? 0) - 90) * (Math.PI / 180);
  const nx = cx + Math.cos(rad) * (r - 8);
  const ny = cy + Math.sin(rad) * (r - 8);

  const cardinals = [
    { label: 'N', angle: 0 },
    { label: 'E', angle: 90 },
    { label: 'S', angle: 180 },
    { label: 'W', angle: 270 },
  ];

  return (
    <div className="flex flex-col items-center">
      <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`}>
        {/* Outer ring */}
        <circle
          cx={cx}
          cy={cy}
          r={r}
          fill="none"
          stroke="#30363d"
          strokeWidth={2}
        />
        {/* Tick marks every 30° */}
        {Array.from({ length: 12 }, (_, i) => {
          const a = (i * 30 - 90) * (Math.PI / 180);
          const x1 = cx + Math.cos(a) * (r - 4);
          const y1 = cy + Math.sin(a) * (r - 4);
          const x2 = cx + Math.cos(a) * r;
          const y2 = cy + Math.sin(a) * r;
          return (
            <line
              key={i}
              x1={x1}
              y1={y1}
              x2={x2}
              y2={y2}
              stroke="#30363d"
              strokeWidth={1.5}
            />
          );
        })}
        {/* Cardinal labels */}
        {cardinals.map(({ label, angle }) => {
          const a = (angle - 90) * (Math.PI / 180);
          const x = cx + Math.cos(a) * (r - 12);
          const y = cy + Math.sin(a) * (r - 12);
          return (
            <text
              key={label}
              x={x}
              y={y}
              textAnchor="middle"
              dominantBaseline="central"
              fontSize={11}
              fill="#7d8590"
              fontFamily="monospace"
            >
              {label}
            </text>
          );
        })}
        {/* Needle */}
        {bearingDeg !== null && (
          <>
            <line
              x1={cx}
              y1={cy}
              x2={nx}
              y2={ny}
              stroke={color}
              strokeWidth={3}
              strokeLinecap="round"
            />
            <circle cx={cx} cy={cy} r={4} fill={color} />
          </>
        )}
      </svg>
      <div className="font-mono text-lg font-bold mt-1" style={{ color }}>
        {bearingDeg !== null ? `${Math.round(bearingDeg)}°` : '—'}
      </div>
    </div>
  );
}

export function BearingPanel() {
  const observations = useAnchorObservations();
  const latest = useLatestBearing();

  // Bearing-over-time series: only confident observations, with an
  // uncertainty band of ±15°.
  const series = useMemo(() => {
    return observations
      .filter(
        (o) => o.confidenceScore > 0.3 && o.bearingEstimateDeg !== null
      )
      .map((o) => ({
        time: formatUptimeMMSS(o.uptimeMs),
        bearing: o.bearingEstimateDeg as number,
        band: [
          (o.bearingEstimateDeg as number) - 15,
          (o.bearingEstimateDeg as number) + 15,
        ] as [number, number],
      }));
  }, [observations]);

  // Most recent RSSI comparison for the stat card
  const latestPair = useMemo(() => {
    for (let i = observations.length - 1; i >= 0; i--) {
      const o = observations[i];
      if (o.rssiNode1 !== null || o.rssiNode2 !== null) return o;
    }
    return null;
  }, [observations]);

  const delta = latestPair?.rssiDelta ?? null;

  return (
    <div className="bg-surface border border-border rounded-lg p-4 shadow">
      <h3 className="font-mono text-sm font-semibold text-accent mb-4 uppercase tracking-wider">
        Bearing estimation
      </h3>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-4 mb-4">
        {/* Compass rose */}
        <div className="flex items-center justify-center bg-base border border-border rounded-lg p-4">
          <CompassRose
            bearingDeg={latest?.bearingEstimateDeg ?? null}
            confidence={latest?.confidenceScore ?? 0}
          />
        </div>

        {/* Node RSSI comparison */}
        <div className="bg-base border border-border rounded-lg p-4 flex flex-col justify-center gap-3 text-sm font-mono">
          <div className="text-text-secondary uppercase tracking-wider text-xs">
            Node RSSI comparison
          </div>
          <div className="flex justify-between">
            <span className="text-text-secondary">Anchor RSSI node 1:</span>
            <span className="font-bold text-text-primary">
              {latestPair?.rssiNode1 != null
                ? `${latestPair.rssiNode1} dBm`
                : '—'}
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-text-secondary">Anchor RSSI node 2:</span>
            <span className="font-bold text-text-primary">
              {latestPair?.rssiNode2 != null
                ? `${latestPair.rssiNode2} dBm`
                : '—'}
            </span>
          </div>
          <div className="flex justify-between border-t border-border pt-2">
            <span className="text-text-secondary">Delta:</span>
            <span
              className={`font-bold ${
                delta == null
                  ? 'text-text-secondary'
                  : delta >= 0
                    ? 'text-signal-strong'
                    : 'text-signal-mid'
              }`}
            >
              {delta != null ? `${delta > 0 ? '+' : ''}${delta} dBm` : '—'}
            </span>
          </div>
          <div className="flex justify-between">
            <span className="text-text-secondary">Confidence:</span>
            <span className="font-bold text-accent">
              {latest ? latest.confidenceScore.toFixed(2) : '0.00'}
            </span>
          </div>
        </div>
      </div>

      {/* Bearing over time */}
      <div className="text-text-secondary uppercase tracking-wider text-xs font-mono mb-2">
        Bearing over time
      </div>
      {series.length > 0 ? (
        <ResponsiveContainer width="100%" height={220}>
          <ComposedChart
            data={series}
            margin={{ top: 10, right: 30, left: 0, bottom: 0 }}
          >
            <CartesianGrid
              strokeDasharray="3 3"
              stroke="#30363d"
              vertical={false}
            />
            <XAxis
              dataKey="time"
              tick={{ fontSize: 11, fill: '#7d8590' }}
              interval={Math.max(0, Math.floor(series.length / 6))}
            />
            <YAxis
              domain={[0, 360]}
              ticks={[0, 90, 180, 270, 360]}
              tick={{ fontSize: 11, fill: '#7d8590' }}
            />
            <Tooltip
              contentStyle={{
                backgroundColor: '#161b22',
                border: '1px solid #30363d',
                borderRadius: '6px',
              }}
              labelStyle={{ color: '#00d4aa' }}
              formatter={(value, name) => {
                if (name === 'bearing')
                  return [`${Math.round(value as number)}°`, 'Bearing'];
                return [value as number, String(name)];
              }}
            />
            {/* ±15° uncertainty band */}
            <Area
              dataKey="band"
              stroke="none"
              fill="#00d4aa"
              fillOpacity={0.12}
              isAnimationActive={false}
            />
            <Line
              type="monotone"
              dataKey="bearing"
              stroke="#00d4aa"
              strokeWidth={2}
              dot={false}
              isAnimationActive={true}
            />
          </ComposedChart>
        </ResponsiveContainer>
      ) : (
        <div className="h-[120px] flex items-center justify-center text-text-secondary text-xs font-mono">
          Not enough confident observations to plot a bearing timeline
        </div>
      )}
    </div>
  );
}
