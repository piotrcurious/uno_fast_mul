
import matplotlib.pyplot as plt
import re

def verify_paths():
    segments = []
    samples = []

    with open("glyph_paths.h", "r") as f:
        content = f.read()

        # Parse segments (MASTER_PATH)
        seg_matches = re.findall(r"\{\s*([-0-9.]+f?),\s*([-0-9.]+f?),\s*([-0-9.]+f?),\s*([-0-9.]+f?)\s*\}",
                                 re.search(r"MASTER_PATH.*?\{(.*?)\};", content, re.DOTALL).group(1))
        for m in seg_matches:
            segments.append([float(x.replace('f','')) for x in m])

        # Parse samples (ALL_VERSE_CHARS)
        samp_matches = re.search(r"ALL_VERSE_CHARS.*?\{(.*?)\};", content, re.DOTALL)
        if samp_matches:
            # Matches 4 floats per entry
            pts = re.findall(r"([-0-9.]+f?),\s*([-0-9.]+f?),\s*([-0-9.]+f?),\s*([-0-9.]+f?)", samp_matches.group(1))
            for p in pts:
                samples.append([float(x.replace('f','')) for x in p])

    plt.figure(figsize=(15, 5))
    for s in segments:
        plt.plot([s[0], s[2]], [-s[1], -s[3]], 'b-', alpha=0.3)

    for i, s in enumerate(samples):
        # Every Nth char to avoid clutter
        if i % 10 == 0:
            plt.plot(s[0], -s[1], 'ro', markersize=2)
            # plt.text(s[0], -s[1], str(i), fontsize=6)
        else:
            plt.plot(s[0], -s[1], 'r.', markersize=1)

    plt.axis('equal')
    plt.title("Path Verification - 'Le fractal est immense' with Verse Characters")
    plt.savefig("path_verification.png")
    print("Verification image saved to path_verification.png")

if __name__ == "__main__":
    verify_paths()
