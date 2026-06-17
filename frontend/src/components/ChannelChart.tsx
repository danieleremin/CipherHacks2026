'use client';

import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';
import { useSessionStore } from '@/store/session';
import { useMemo } from 'react';

export function ChannelChart() {
  const session = useSessionStore((s) => s.session);

  const data = useMemo(() => {
    if (!session) return [];
    const dist = session.summary.channelDistribution;
    return Array.from({ length: 13 }, (_, i) => ({
      channel: (i + 1).toString(),
      count: dist[i + 1] ?? 0,
      nonOverlap: [1, 6, 11].includes(i + 1),
    }));
  }, [session]);

  return (
    <ResponsiveContainer width="100%" height={250}>
      <BarChart data={data} margin={{ top: 20, right: 30, left: 0, bottom: 0 }}>
        <CartesianGrid strokeDasharray="3 3" stroke="#30363d" vertical={false} />
        <XAxis
          dataKey="channel"
          tick={({ x, y, payload }) => {
            const isNonOverlap = [1, 6, 11].includes(parseInt(payload.value));
            return (
              <text
                x={x}
                y={y}
                textAnchor="middle"
                fill={isNonOverlap ? '#00d4aa' : '#7d8590'}
                fontSize={11}
                fontWeight={isNonOverlap ? 'bold' : 'normal'}
              >
                {payload.value}
              </text>
            );
          }}
        />
        <YAxis tick={{ fontSize: 11, fill: '#7d8590' }} />
        <Tooltip
          contentStyle={{
            backgroundColor: '#161b22',
            border: '1px solid #30363d',
            borderRadius: '6px',
          }}
          labelStyle={{ color: '#00d4aa' }}
        />
        <Bar
          dataKey="count"
          fill="#00d4aa"
          isAnimationActive={true}
          radius={[4, 4, 0, 0]}
        />
      </BarChart>
    </ResponsiveContainer>
  );
}
