# AMD DirectX12 PSO Crash
Small reproduction sample for DX12 Compute PSO creation on AMD GPUs
There is also same shader in binary form in "shader.h" include.

# Building
1) Get DXC binaries 1.8.2502 from https://github.com/microsoft/DirectXShaderCompiler
2) Get Agility SDK 1.615 from https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.615.1
3) Add DXC and Agility SDK binaries into ``Binaries\Win64\`` and ``Binaries\Win64\D3D12\`` respectively
4) Build in VS22

# Environment
- Visual Studio 2022
- Windows SDK 10.0.22621
- Platform Toolset: Visual Studio 2022 v143
