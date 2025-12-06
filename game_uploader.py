import subprocess
import os
import sys

MK_SPIFFS = "./mkspiffs.exe"
ESPTOOL = r"C:\Users\JK123\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe"

FS_DIR = "spiffs_data"
FS_IMAGE = "spiffs.bin"
FS_OFFSET = "0x510000"  # NUEVA DIRECCIÓN
FS_SIZE = "0x1F0000"    # NUEVO TAMAÑO: ~1.93MB
PORT = "COM11"
BAUD = "921600"

def run(cmd):
    print(" ".join(cmd))
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print("❌ Error ejecutando:", cmd)
        return False
    return True

def check_spiffs_usage():
    """Verifica el tamaño total de los archivos en spiffs_data"""
    if not os.path.exists(FS_DIR):
        print(f"📁 Directorio {FS_DIR} no encontrado. Creándolo...")
        os.makedirs(FS_DIR)
        return 0, 0
        
    total_size = 0
    file_count = 0
    print("\n📊 Analizando uso del directorio SPIFFS:")
    
    for dirpath, dirnames, filenames in os.walk(FS_DIR):
        for filename in filenames:
            filepath = os.path.join(dirpath, filename)
            file_size = os.path.getsize(filepath)
            total_size += file_size
            file_count += 1
            print(f"  {filename}: {file_size} bytes")
    
    return total_size, file_count

def erase_spiffs():
    """Borra la región de SPIFFS en el ESP32"""
    print(f"🧹 Borrando SPIFFS del ESP32 en {FS_OFFSET} de tamaño {FS_SIZE}...")
    return run([
        ESPTOOL,
        "--chip", "esp32s3",
        "--port", PORT, 
        "--baud", BAUD,
        "erase_region",
        FS_OFFSET,
        FS_SIZE
    ])

def build_spiffs():
    print("\n📦 Generando imagen SPIFFS...")
    
    total_size, file_count = check_spiffs_usage()
    fs_size_bytes = int(FS_SIZE, 16)
    
    print(f"\n📈 Resumen:")
    print(f"  Archivos: {file_count}") 
    print(f"  Tamaño total de archivos: {total_size} bytes")
    print(f"  Tamaño de SPIFFS: {fs_size_bytes} bytes")
    print(f"  Espacio utilizado: {(total_size/fs_size_bytes)*100:.1f}%")
    
    # Configuración optimizada
    return run([
        MK_SPIFFS,
        "-c", FS_DIR,
        "-b", "4096",
        "-p", "256", 
        "-s", FS_SIZE,
        FS_IMAGE
    ])

def flash_spiffs():
    print("\n⚡ Flasheando SPIFFS al ESP32-S3...")
    return run([
        ESPTOOL,
        "--chip", "esp32s3",
        "--port", PORT,
        "--baud", BAUD, 
        "write_flash",  # ❌ ERA "write_flush" - ✅ CORREGIR A "write_flash"
        FS_OFFSET,
        FS_IMAGE
    ])

def cleanup():
    """Limpia archivos temporales"""
    if os.path.exists(FS_IMAGE):
        os.remove(FS_IMAGE)
        print(f"🧹 Archivo temporal {FS_IMAGE} eliminado")

if __name__ == "__main__":
    try:
        print("🚀 Iniciando flasheo SPIFFS con nueva partition table...")
        
        if not erase_spiffs():
            sys.exit(1)
            
        if not build_spiffs():
            print("\n❌ Error creando imagen SPIFFS")
            sys.exit(1)
            
        if flash_spiffs():
            print("\n✅ SPIFFS flasheado correctamente con nuevo tamaño!")
            print("💾 Espacio disponible: ~1.93MB")
        else:
            sys.exit(1)
            
    finally:
        cleanup()