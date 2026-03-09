export default defineNuxtConfig({
  compatibilityDate: '2025-07-15',
  devtools: { enabled: true },

  ssr: false,
  nitro: {
    devProxy: {
      '/api': {
        target: 'http://localhost:8080/api',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://localhost:8080/ws',
        ws: true,
        changeOrigin: true,
      }
    }
  }
})
