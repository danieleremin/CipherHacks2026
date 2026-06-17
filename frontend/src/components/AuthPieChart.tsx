'use client';

import { PieChart, Pie, Cell, ResponsiveContainer, Legend, Tooltip } from 'recharts';
import { useSessionStore } from '@/store/session';
import { useMemo } from 'react';

export function AuthPieChart() {
  const session = useSessionStore((s) => s.session);

  const data = useMemo(() => {
    if (!session) return [];
    const dist = session.summary.authModeDistribution;
    return Object.entries(dist)
      .filter(([_, count]) => count > 0)
      .map(([authMode, count]) => ({
        name: authMode,
        value: count,
      }));
  }, [session]);

  const COLORS: Record<string, string> = {
    OPEN: '#ef4444',
    WEP: '#f97316',
    WPA: '#eab308',
    WPA2: '#22c55e',
    WPA3: '#3b82f6',
    'WPA2/WPA3': '#6366f1',
    UNKNOWN: '#6b7280',
  };

  return (
    <ResponsiveContainer width="100%" height={250}>
      <PieChart>
        <Pie
          data={data}
          cx="50%"
          cy="50%"
          labelLine={false}
          label={({ name, percent }) =>
            `${name} ${(typeof percent === 'number' ? (percent * 100).toFixed(0) : '0')}%`
          }
          outerRadius={60}
          fill="#8884d8"
          dataKey="value"
        >
          {data.map((entry, index) => (
            <Cell
              key={`cell-${index}`}
              fill={COLORS[entry.name] || COLORS.UNKNOWN}
            />
          ))}
        </Pie>
        <Tooltip
          contentStyle={{
            backgroundColor: '#161b22',
            border: '1px solid #30363d',
            borderRadius: '6px',
          }}
          labelStyle={{ color: '#00d4aa' }}
        />
      </PieChart>
    </ResponsiveContainer>
  );
}
