# DirectX 9 / Direct3D 9Ex Official Dependency Documentation

Este directorio contiene las especificaciones, manifiestos de procedencia y scripts de automatización para la infraestructura gráfica oficial de **DirectX 9 / Direct3D 9Ex** en **UNCHARTED: Sic Parvis Magna Collection**.

## 1. Procedencia y Distribución Oficial
- **Nombre:** DirectX End-User Runtimes (June 2010)
- **Editor:** Microsoft Corporation
- **Portal Oficial:** [Microsoft Download Center ID 8109](https://www.microsoft.com/download/details.aspx?id=8109)
- **Enlace de Descarga Directa:** [directx_Jun2010_redist.exe](https://download.microsoft.com/download/8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14/directx_Jun2010_redist.exe)
- **Hash SHA-256 Oficial:** `439c277085c88b9a2be741d440ad82e88a38a9bc6723b7aa9a4569e12de7bc71`

## 2. Subsistema del Sistema Operativo
- **Controlador Nativo:** `C:\Windows\System32\d3d9.dll`
- **Modelo de Dispositivo:** Direct3D 9Ex (`Direct3DCreate9Ex`, disponible de forma nativa desde Windows Vista, 7, 8, 8.1, 10 y 11).
- **Modo de Presentación:** `D3DSWAPEFFECT_FLIPEX` con fallback a `D3DSWAPEFFECT_DISCARD`.
- **Interoperabilidad:** Superficies compartidas de renderizado (`D3DPOOL_DEFAULT` con `HANDLE* pSharedHandle`).

## 3. Scripts de Comprobación y Automatización
- **`Check-DirectX9Requirements.ps1`:** Comprueba la presencia de las librerías del sistema, soporte del punto de entrada `Direct3DCreate9Ex`, compiladores de shaders (`d3dcompiler_47.dll`, `d3dcompiler_43.dll`) y componentes `d3dx9_43.dll`.
- **`Install-DirectX9Runtimes.ps1`:** Descarga el paquete oficial acumulativo desde Microsoft, comprueba su firma criptográfica SHA-256, lo extrae y prepara la instalación silenciosa mediante `DXSETUP.exe`.
