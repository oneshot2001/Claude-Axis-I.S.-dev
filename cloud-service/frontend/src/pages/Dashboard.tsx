import React, { useState, useEffect } from 'react'
import { useWebSocketContext } from '../context/WebSocketContext'

interface Camera {
  camera_id: string
  state: {
    state: string
    version: string
    timestamp: number
  }
}

const Dashboard: React.FC = () => {
  const [cameras, setCameras] = useState<Camera[]>([])
  const [loading, setLoading] = useState(true)
  const { lastMessage } = useWebSocketContext()

  useEffect(() => {
    const fetchCameras = async () => {
      try {
        const res = await fetch('/api/cameras')
        if (res.ok) {
          const data = await res.json()
          setCameras(data.cameras)
        }
      } catch (error) {
        console.error('Failed to fetch cameras', error)
      } finally {
        setLoading(false)
      }
    }

    fetchCameras()
    const interval = setInterval(fetchCameras, 10000) // Reduced polling rate due to WS
    return () => clearInterval(interval)
  }, [])

  // Handle real-time updates
  useEffect(() => {
    if (!lastMessage) return

    if (lastMessage.type === 'camera_status') {
      const { camera_id, status } = lastMessage.payload
      setCameras(prev => {
        const exists = prev.find(c => c.camera_id === camera_id)
        if (exists) {
          return prev.map(c => c.camera_id === camera_id ? { ...c, state: status } : c)
        }
        return [...prev, { camera_id, state: status }]
      })
    }
  }, [lastMessage])

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-bold text-gray-900">System Overview</h1>
        <button 
          className="px-4 py-2 bg-axis-yellow text-axis-black font-semibold rounded shadow-sm hover:bg-yellow-400 transition-colors"
          onClick={() => window.location.reload()}
        >
          Refresh
        </button>
      </div>

      {/* Stats Grid */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
        <div className="bg-white p-6 rounded-lg shadow-sm border border-gray-200">
          <div className="text-sm text-gray-500 font-medium">Active Cameras</div>
          <div className="mt-2 text-3xl font-bold text-gray-900">{cameras.length}</div>
        </div>
        <div className="bg-white p-6 rounded-lg shadow-sm border border-gray-200">
          <div className="text-sm text-gray-500 font-medium">Events (24h)</div>
          <div className="mt-2 text-3xl font-bold text-gray-900">0</div>
        </div>
        <div className="bg-white p-6 rounded-lg shadow-sm border border-gray-200">
          <div className="text-sm text-gray-500 font-medium">Alerts</div>
          <div className="mt-2 text-3xl font-bold text-status-offline">0</div>
        </div>
      </div>

      {/* Camera Grid */}
      <div className="bg-white rounded-lg shadow-sm border border-gray-200 overflow-hidden">
        <div className="px-6 py-4 border-b border-gray-200">
          <h2 className="text-lg font-semibold text-gray-900">Connected Devices</h2>
        </div>
        
        {loading ? (
          <div className="p-8 text-center text-gray-500">Loading devices...</div>
        ) : cameras.length === 0 ? (
          <div className="p-8 text-center text-gray-500">No cameras connected</div>
        ) : (
          <div className="divide-y divide-gray-200">
            {cameras.map((cam) => (
              <div key={cam.camera_id} className="p-6 flex items-center justify-between hover:bg-gray-50 transition-colors">
                <div className="flex items-center gap-4">
                  <div className={`w-3 h-3 rounded-full ${
                    cam.state.state === 'online' ? 'bg-status-online' : 'bg-status-offline'
                  }`} />
                  <div>
                    <div className="font-medium text-gray-900">{cam.camera_id}</div>
                    <div className="text-sm text-gray-500">Version: {cam.state.version}</div>
                  </div>
                </div>
                <div className="flex items-center gap-4 text-sm text-gray-500">
                  <span>Last seen: {new Date(cam.state.timestamp * 1000).toLocaleTimeString()}</span>
                  <button className="text-axis-black font-medium hover:underline">View Details</button>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  )
}

export default Dashboard

