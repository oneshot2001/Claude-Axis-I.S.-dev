import React from 'react'
import { Link } from 'react-router-dom'

const Header: React.FC = () => {
  return (
    <header className="bg-axis-black text-axis-white border-b-4 border-axis-yellow shadow-md">
      <div className="container mx-auto px-4 h-16 flex items-center justify-between">
        <div className="flex items-center gap-8">
          <Link to="/" className="text-xl font-bold tracking-tight">
            Axis <span className="text-axis-yellow">I.S.</span> Hub
          </Link>
          
          <nav className="hidden md:flex items-center gap-6 text-sm font-medium">
            <Link to="/" className="hover:text-axis-yellow transition-colors">Dashboard</Link>
            <span className="text-gray-500 cursor-not-allowed">Analytics</span>
            <span className="text-gray-500 cursor-not-allowed">Investigations</span>
          </nav>
        </div>

        <div className="flex items-center gap-4">
          <div className="flex items-center gap-2">
            <div className="w-2 h-2 rounded-full bg-status-online"></div>
            <span className="text-xs font-medium">System Online</span>
          </div>
        </div>
      </div>
    </header>
  )
}

export default Header

