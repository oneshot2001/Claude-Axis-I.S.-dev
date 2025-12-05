import React from 'react'
import { BrowserRouter as Router, Routes, Route } from 'react-router-dom'
import Layout from './components/Layout'
import Dashboard from './pages/Dashboard'
import { WebSocketProvider } from './context/WebSocketContext'

function App() {
  return (
    <WebSocketProvider>
      <Router>
        <Routes>
          <Route path="/" element={<Layout />}>
            <Route index element={<Dashboard />} />
            {/* Future routes: /cameras/:id, /analytics, /investigations */}
          </Route>
        </Routes>
      </Router>
    </WebSocketProvider>
  )
}

export default App

