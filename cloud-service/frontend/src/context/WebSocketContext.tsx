import React, { createContext, useContext, useEffect, useState } from 'react'
import { useWebSocket } from '../hooks/useWebSocket'

interface WebSocketContextType {
  isConnected: boolean
  lastMessage: any
  sendMessage: (msg: any) => void
}

const WebSocketContext = createContext<WebSocketContextType | null>(null)

export const WebSocketProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  // Connect to backend WS endpoint
  // In dev (vite proxy), this will go to ws://localhost:8000/ws
  const { isConnected, lastMessage, sendMessage } = useWebSocket('/ws')

  return (
    <WebSocketContext.Provider value={{ isConnected, lastMessage, sendMessage }}>
      {children}
    </WebSocketContext.Provider>
  )
}

export const useWebSocketContext = () => {
  const context = useContext(WebSocketContext)
  if (!context) {
    throw new Error('useWebSocketContext must be used within a WebSocketProvider')
  }
  return context
}

