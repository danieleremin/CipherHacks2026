import type { NextConfig } from 'next';

const nextConfig: NextConfig = {
  // Static export for USB drive / web server deployment
  output: 'export',

  // Image optimization configuration
  images: {
    remotePatterns: [
      {
        protocol: 'https',
        hostname: '*.tile.openstreetmap.org',
      },
    ],
    unoptimized: true, // Required for static export
  },

  // Support for client-side navigation without server
  trailingSlash: false,
};

export default nextConfig;
