'use client';

import { AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';
import { useFilteredDetections } from '@/lib/filters';
import { useMemo } from 'react';
import { format } from 'date-fns';

export function TimelineChart() {
  const filtered = useFilteredDetections();

  const data = useMemo(() => {
    if (filtered.length === 0) return [];

    // Group detections by minute
    const buckets = new Map<number, number>();
    for (const d of filtered) {
      // Round to nearest minute
      const minute = Math.floor(d.firstSeen.getTime() / 60000) * 60000;
      buckets.set(minute, (buckets.get(minute) ?? 0) + 1);
    }

    // Convert to sorted array
    return Array.from(buckets.entries())
      .sort((a, b) => a[0] - b[0])
      .map(([timestamp, count]) => ({
        time: format(new Date(timestamp), 'HH:mm'),
        count,
        timestamp,
      }));
  }, [filtered]);

  return (
    <ResponsiveContainer width="100%" height={250}>
      <AreaChart data={data} margin={{ top: 20, right: 30, left: 0, bottom: 0 }}>
        <defs>
          <linearGradient id="colorCount" x1="0" y1="0" x2="0" y2="1">
            <stop offset="5%" stopColor="#00d4aa" stopOpacity={0.8} />
            <stop offset="95%" stopColor="#00d4aa" stopOpacity={0.1} />
          </linearGradient>
        </defs>
        <CartesianGrid strokeDasharray="3 3" stroke="#30363d" vertical={false} />
        <XAxis
          dataKey="time"
          tick={{ fontSize: 11, fill: '#7d8590' }}
          interval={Math.max(0, Math.floor(data.length / 6))}
        />
        <YAxis tick={{ fontSize: 11, fill: '#7d8590' }} />
        <Tooltip
          contentStyle={{
            backgroundColor: '#161b22',
            border: '1px solid #30363d',
            borderRadius: '6px',
          }}
          labelStyle={{ color: '#00d4aa' }}
          formatter={(value) => [`${value} detections`, 'Count']}
        />
        <Area
          type="monotone"
          dataKey="count"
          stroke="#00d4aa"
          fillOpacity={1}
          fill="url(#colorCount)"
          isAnimationActive={true}
        />
      </AreaChart>
    </ResponsiveContainer>
  );
}
