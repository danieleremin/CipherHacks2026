'use client';

import { useEffect } from 'react';
import { MapContainer, TileLayer, Popup, useMap, Marker } from 'react-leaflet';
import L from 'leaflet';
import MarkerClusterGroup from 'react-leaflet-cluster';
import { useSessionStore } from '@/store/session';
import type { Detection } from '@/types/detection';

// Fix Leaflet icon issue
delete (L.Icon.Default.prototype as any)._getIconUrl;
L.Icon.Default.mergeOptions({
  iconRetinaUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/images/marker-icon-2x.png',
  iconUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/images/marker-icon.png',
  shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/images/marker-shadow.png',
});

interface MapContentProps {
  detections: Detection[];
  networkGroups: Map<string, Detection[]>;
  selectedMac: string | null;
  onSelectNetwork: (mac: string | null) => void;
}

// Auth mode to color mapping
const AUTH_COLORS: Record<string, string> = {
  OPEN: '#ef4444',     // red
  WEP: '#f97316',      // orange
  WPA: '#eab308',      // yellow
  WPA2: '#22c55e',     // green
  WPA3: '#3b82f6',     // blue
  'WPA2/WPA3': '#6366f1', // indigo
  UNKNOWN: '#6b7280',  // gray
};

function createMarkerIcon(authMode: string, selected: boolean = false) {
  const color = AUTH_COLORS[authMode] || AUTH_COLORS.UNKNOWN;
  const size = selected ? 32 : 24;

  return L.divIcon({
    html: `<div style="background-color: ${color}; width: ${size}px; height: ${size}px; border-radius: 50%; border: ${selected ? '3px solid #00d4aa' : '2px solid rgba(0,0,0,0.5)'}; box-shadow: 0 2px 8px rgba(0,0,0,0.4);"></div>`,
    iconSize: [size, size],
    popupAnchor: [0, -size / 2],
  });
}

// Map controls component
function MapControls() {
  const map = useMap();

  useEffect(() => {
    let bounds: L.LatLngBounds | null = null;

    map.eachLayer((layer: any) => {
      if (layer.getBounds) {
        const layerBounds = layer.getBounds();
        if (layerBounds && typeof (layerBounds as any).isValid === 'function' && (layerBounds as any).isValid()) {
          bounds = bounds ? bounds.extend(layerBounds) : layerBounds;
        }
      } else if (layer.getLatLng) {
        const latlng = layer.getLatLng();
        bounds = bounds ? bounds.extend(latlng) : L.latLngBounds(latlng, latlng);
      }
    });

    if (bounds && typeof (bounds as any).isValid === 'function' && (bounds as any).isValid()) {
      map.fitBounds(bounds, { padding: [50, 50] });
    }
  }, [map]);

  return null;
}

export function MapContent({
  detections,
  networkGroups,
  selectedMac,
  onSelectNetwork,
}: MapContentProps) {
  // Default map center (San Diego area)
  const defaultCenter: [number, number] = [32.7157, -117.1611];
  const defaultZoom = 13;

  return (
    <MapContainer
      center={defaultCenter}
      zoom={defaultZoom}
      style={{ width: '100%', height: '100%' }}
      scrollWheelZoom={true}
    >
      {/* Base tile layer */}
      <TileLayer
        attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
        url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
      />

      {/* Map controls */}
      <MapControls />

      {/* Marker cluster group */}
      <MarkerClusterGroup chunkedLoading>
        {detections.map((detection, idx) => {
          // Skip invalid GPS fixes
          if (detection.lat === 0 && detection.lon === 0) {
            return null;
          }

          const isSelected = detection.mac === selectedMac;
          const icon = createMarkerIcon(detection.authMode, isSelected);

          return (
            <Marker
              key={`${detection.mac}-${idx}`}
              position={[detection.lat, detection.lon]}
              icon={icon}
              eventHandlers={{
                click: () => onSelectNetwork(detection.mac),
              }}
            >
              <Popup>
                <div className="font-sans text-sm p-2">
                  <div className="font-mono font-bold text-accent mb-1">
                    {detection.ssid || '(hidden)'}
                  </div>
                  <div className="font-mono text-xs text-text-secondary mb-2">
                    {detection.mac}
                  </div>
                  {detection.manufacturer && (
                    <div className="text-xs mb-2">{detection.manufacturer}</div>
                  )}
                  <div className="text-xs space-y-1">
                    <div>
                      <span className="text-text-secondary">Ch:</span> {detection.channel}
                    </div>
                    <div>
                      <span className="text-text-secondary">RSSI:</span> {detection.rssi} dBm
                    </div>
                    <div>
                      <span className="text-text-secondary">Auth:</span> {detection.authMode}
                    </div>
                  </div>
                </div>
              </Popup>
            </Marker>
          );
        })}
      </MarkerClusterGroup>
    </MapContainer>
  );
}
