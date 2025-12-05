import { useEffect, useRef, useState, useCallback } from 'react'

interface WebSocketMessage {
  type: string
  payload: any
}

export const useWebSocket = (url: string) => {
  const [isConnected, setIsConnected] = useState(false)
  const [lastMessage, setLastMessage] = useState<WebSocketMessage | null>(null)
  const ws = useRef<WebSocket | null>(null)
  const reconnectTimeout = useRef<NodeJS.Timeout>()

  const connect = useCallback(() => {
    // Clean up existing connection
    if (ws.current) {
      ws.current.close()
    }

    // Use relative URL if starting with /
    const wsUrl = url.startsWith('/') 
      ? `ws://${window.location.host}${url}`
      : url

    console.log('Connecting to WebSocket:', wsUrl)
    ws.current = new WebSocket(wsUrl)

    ws.current.onopen = () => {
      console.log('WebSocket Connected')
      setIsConnected(true)
    }

    ws.current.onclose = () => {
      console.log('WebSocket Disconnected')
      setIsConnected(false)
      // Reconnect after 3 seconds
      reconnectTimeout.current = setTimeout(connect, 3000)
    }

    ws.current.onerror = (error) => {
      console.error('WebSocket Error:', error)
      ws.current?.close()
    }

    ws.current.onmessage = (event) => {
      try {
        const message = JSON.parse(event.data)
        setLastMessage(message)
      } catch (e) {
        console.error('Failed to parse WebSocket message:', event.data)
      }
    }
  }, [url])

  useEffect(() => {
    connect()
    return () => {
      if (ws.current) {
        ws.current.close()
      }
      if (reconnectTimeout.current) {
        clearTimeout(reconnectTimeout.current)
      }
    }
  }, [connect])

  const sendMessage = useCallback((msg: WebSocketMessage) => {
    if (ws.current && ws.current.readyState === WebSocket.OPEN) {
      ws.current.send(JSON.stringify(msg))
    } else {
      console.warn('WebSocket not connected, cannot send message')
    }
  }, [])

  return { isConnected, lastMessage, sendMessage }
}

