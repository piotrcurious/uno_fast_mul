from PIL import Image
import sys
def convert(ppm_file, png_file):
    img = Image.open(ppm_file)
    img.save(png_file)
if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
