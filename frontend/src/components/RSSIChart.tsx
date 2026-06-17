'use client';

import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts';
import { useSessionStore } from '@/store/session';

export function RSSIChart() {
  const session = useSessionStore((s) => s.session);

  if (!session) return null;

  const data = session.summary.rssiHistogram;

  return (
    <ResponsiveContainer width="100%" height={250}>
      <BarChart data={data} margin={{ top: 20, right: 30, left: 0, bottom: 0 }}>
        <CartesianGrid strokeDasharray="3 3" stroke="#30363d" vertical={false} />
        <XAxis
          dataKey="bucket"
          tick={{ fontSize: 11, fill: '#7d8590' }}
          angle={-45}
          textAnchor="end"
          height={80}
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
