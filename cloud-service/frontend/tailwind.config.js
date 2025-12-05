/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        axis: {
          black: '#000000',
          yellow: '#FFD200',
          red: '#B22234',
          white: '#FFFFFF',
        },
        status: {
          online: '#22C55E',
          offline: '#EF4444',
          degraded: '#F59E0B',
        }
      },
      fontFamily: {
        sans: ['"Segoe UI"', 'Tahoma', 'Geneva', 'Verdana', 'sans-serif'],
      }
    },
  },
  plugins: [],
}

