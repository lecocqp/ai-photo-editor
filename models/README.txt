AI Photo Editor - ONNX Model Files
====================================

Place your ONNX model files in this directory for AI-powered features.

Expected model filenames:
  realesrgan-x2.onnx    - Real-ESRGAN 2x upscaling
  realesrgan-x4.onnx    - Real-ESRGAN 4x upscaling
  denoise.onnx          - Denoising autoencoder
  sharpen.onnx          - Sharpening network
  gfpgan.onnx           - GFPGAN face restoration
  codeformer.onnx       - CodeFormer face restoration (alternative)
  enhance.onnx          - General image enhancement

If no models are found, the editor falls back to classical algorithms:
  - Upscale: Lanczos resampling (via OpenImageIO)
  - Denoise: Bilateral filter
  - Sharpen: Unsharp mask
  - Face Restore: Bilateral smooth + detail sharpen
  - Enhance: Auto levels + contrast + saturation + S-curve

Where to get models:
  - Real-ESRGAN: https://github.com/xinntao/Real-ESRGAN
  - GFPGAN: https://github.com/TencentARC/GFPGAN
  - CodeFormer: https://github.com/sczhou/CodeFormer

Models must be exported to ONNX format. Input/output format:
  - Input:  NCHW float32, range [0, 1], 3 channels (RGB)
  - Output: NCHW float32, range [0, 1], 3 channels (RGB)
