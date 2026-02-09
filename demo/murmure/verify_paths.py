
import matplotlib.pyplot as plt
import re

def verify_paths():
    segments = []
    samples = []

    with open("glyph_paths.h", "r") as f:
        content = f.read()

        # Parse segments
        seg_matches = re.findall(r"\{\s*([-0-9.]+f?),\s*([-0-9.]+f?),\s*([-0-9.]+f?),\s*([-0-9.]+f?)\s*\}", content)
        for m in seg_matches:
            segments.append([float(x.replace('f','')) for x in m])

        # Parse samples
        samp_matches = re.findall(r"VERSE_POSITIONS.*?\{(.*?)\}", content, re.DOTALL)
        if samp_matches:
            pts = re.findall(r"([-0-9.]+f?),\s*([-0-9.]+f?),\s*([-0-9.]+f?)", samp_matches[0])
            for p in pts:
                samples.append([float(x.replace('f','')) for x in p])

    plt.figure(figsize=(15, 5))
    for s in segments:
        plt.plot([s[0], s[2]], [-s[1], -s[3]], 'b-', alpha=0.5)

    for i, s in enumerate(samples):
        plt.plot(s[0], -s[1], 'ro')
        plt.text(s[0], -s[1], str(i+1), fontsize=8)

    plt.axis('equal')
    plt.title("Path Verification - 'Le fractal est immense'")
    plt.savefig("path_verification.png")
    print("Verification image saved to path_verification.png")

if __name__ == "__main__":
    verify_paths()
