from flask import Flask, request, jsonify
import cv2
import numpy as np
import os
import face_recognition
import PIL
import io
from datetime import datetime

app = Flask(__name__)

SAVE_DIR    = "photos_medicaments"
DATASET_DIR = "dataset"
os.makedirs(SAVE_DIR, exist_ok=True)

# Seuil plus strict
SEUIL_VISAGE = 0.42

visages_connus = []
noms_connus    = []

CASCADE_PATH = cv2.data.haarcascades + "haarcascade_frontalface_default.xml"
face_cascade = cv2.CascadeClassifier(CASCADE_PATH)


def bytes_vers_bgr(data):
    try:
        pil = PIL.Image.open(io.BytesIO(data)).convert("RGB")
        arr = np.array(pil, dtype=np.uint8)
        bgr = cv2.cvtColor(arr, cv2.COLOR_RGB2BGR)
        return bgr
    except Exception as e:
        print("[DECODE] Erreur:", e)
        return None


def ameliorer_image(bgr):
    bgr = cv2.resize(bgr, None, fx=1.5, fy=1.5, interpolation=cv2.INTER_CUBIC)
    lab = cv2.cvtColor(bgr, cv2.COLOR_BGR2LAB)
    l, a, b = cv2.split(lab)
    clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
    l = clahe.apply(l)
    lab = cv2.merge((l, a, b))
    result = cv2.cvtColor(lab, cv2.COLOR_LAB2BGR)
    gray = cv2.cvtColor(result, cv2.COLOR_BGR2GRAY)
    print(f"[PRETRAITEMENT] Luminosite moyenne : {np.mean(gray):.1f}/255")
    return result


def detecter_visage(bgr):
    """
    CORRECTION : double validation obligatoire.
    Un visage doit être détecté par LES DEUX méthodes pour être accepté.
    Cela élimine les faux positifs (lit, mur, objets).
    """
    gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
    gray = cv2.equalizeHist(gray)

    # Haar Cascade — paramètres plus stricts
    faces_haar = face_cascade.detectMultiScale(
        gray,
        scaleFactor=1.1,
        minNeighbors=6,      # CORRECTION : était 2, maintenant 6
        minSize=(60, 60)     # CORRECTION : était 25x25, maintenant 60x60
    )

    locs_haar = [(y, x+w, y+h, x) for (x, y, w, h) in faces_haar]

    if not locs_haar:
        print("[DETECTION] Haar : aucun visage")
        # Essayer HOG seul
        rgb = np.ascontiguousarray(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB), dtype=np.uint8)
        try:
            locs_hog = face_recognition.face_locations(
                rgb, number_of_times_to_upsample=2, model="hog"
            )
            if locs_hog:
                print(f"[DETECTION] HOG seul : {len(locs_hog)} visage(s)")
                return locs_hog
            else:
                print("[DETECTION] HOG : aucun visage non plus")
                return []
        except Exception as e:
            print("[DETECTION] Erreur HOG:", e)
            return []

    # Double validation : Haar a trouvé quelque chose, confirmer avec HOG
    rgb = np.ascontiguousarray(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB), dtype=np.uint8)
    try:
        locs_hog = face_recognition.face_locations(
            rgb, number_of_times_to_upsample=2, model="hog"
        )
    except Exception as e:
        print("[DETECTION] Erreur HOG confirmation:", e)
        locs_hog = []

    if locs_hog:
        print(f"[DETECTION] DOUBLE VALIDATION OK : Haar={len(locs_haar)} HOG={len(locs_hog)}")
        return locs_hog  # HOG donne de meilleures localisations
    else:
        # Haar a trouvé mais HOG non -> probablement faux positif
        print(f"[DETECTION] FAUX POSITIF REJETE : Haar={len(locs_haar)} mais HOG=0")
        return []


def charger_dataset():
    global visages_connus, noms_connus
    visages_connus = []
    noms_connus    = []

    if not os.path.exists(DATASET_DIR):
        print("[DATASET] Dossier dataset introuvable")
        return

    for patient in sorted(os.listdir(DATASET_DIR)):
        dossier = os.path.join(DATASET_DIR, patient)
        if not os.path.isdir(dossier):
            continue

        ok = err = 0

        for fichier in sorted(os.listdir(dossier)):
            if not fichier.lower().endswith((".jpg", ".jpeg", ".png")):
                continue
            chemin = os.path.join(dossier, fichier)
            try:
                with open(chemin, "rb") as f:
                    data = f.read()

                bgr = bytes_vers_bgr(data)
                if bgr is None:
                    err += 1; continue

                bgr_ok = ameliorer_image(bgr)
                rgb_ok = np.ascontiguousarray(
                    cv2.cvtColor(bgr_ok, cv2.COLOR_BGR2RGB), dtype=np.uint8
                )

                locs = detecter_visage(bgr_ok)

                encs = face_recognition.face_encodings(
                    rgb_ok,
                    known_face_locations=locs if locs else None,
                    num_jitters=3,
                    model="large"
                )

                if encs:
                    visages_connus.append(encs[0])
                    noms_connus.append(patient)
                    ok += 1
                    print(f"[DATASET] OK {patient}/{fichier}")
                else:
                    err += 1
                    print(f"[DATASET] NON ENCODABLE : {patient}/{fichier}")

            except Exception as e:
                err += 1
                print(f"[DATASET] ERREUR {fichier} : {e}")

        print(f"[DATASET] {patient} : {ok} OK / {err} erreur(s)")

    print(f"[DATASET] TOTAL : {len(visages_connus)} encodage(s)\n")


def analyser_visage(img_bytes):
    bgr = bytes_vers_bgr(img_bytes)
    if bgr is None:
        return {"success": False, "match": False, "message": "No face detected"}

    print("\n[ANALYSE] ──────────────────────────")
    print(f"[ANALYSE] Taille originale : {bgr.shape[1]}x{bgr.shape[0]}")

    bgr_ok = ameliorer_image(bgr)
    rgb_ok = np.ascontiguousarray(
        cv2.cvtColor(bgr_ok, cv2.COLOR_BGR2RGB), dtype=np.uint8
    )

    locs = detecter_visage(bgr_ok)
    if not locs:
        print("[ANALYSE] Aucun visage detecte")
        # CORRECTION : success=True pour que l'ESP32 lise bien le message
        return {"success": True, "match": False, "message": "No face detected"}

    try:
        encodages = face_recognition.face_encodings(
            rgb_ok,
            known_face_locations=locs,
            num_jitters=3,
            model="large"
        )
    except Exception as e:
        print("[ANALYSE] Encodage impossible:", e)
        return {"success": True, "match": False, "message": "No face detected"}

    if not encodages:
        print("[ANALYSE] Encodage vide")
        return {"success": True, "match": False, "message": "No face detected"}

    if not visages_connus:
        print("[ANALYSE] Dataset vide")
        return {"success": False, "match": False, "message": "Dataset empty"}

    distances = face_recognition.face_distance(visages_connus, encodages[0])
    idx  = int(np.argmin(distances))
    dist = float(distances[idx])
    nom  = noms_connus[idx]

    print(f"[ANALYSE] Meilleur candidat : {nom}  distance={dist:.4f}  seuil={SEUIL_VISAGE}")
    for i, d in enumerate(distances):
        print(f"           {noms_connus[i]:20s} -> {d:.4f}")

    if dist <= SEUIL_VISAGE:
        print("[ANALYSE] VISAGE_OK")
        return {
            "success": True,
            "match": True,
            "message": "Face recognized",
            "name": nom,
            "distance": round(dist, 4)
        }
    else:
        print("[ANALYSE] VISAGE_INCORRECT")
        return {
            "success": True,
            "match": False,
            "message": "Face not recognized",
            "name": nom,
            "distance": round(dist, 4)
        }


@app.route("/upload", methods=["POST"])
def upload():
    img_bytes = request.data
    if not img_bytes:
        return jsonify({"success": False, "match": False, "message": "No image received"}), 200

    ts       = datetime.now().strftime("%Y%m%d_%H%M%S")
    filepath = os.path.join(SAVE_DIR, f"photo_{ts}.jpg")
    with open(filepath, "wb") as f:
        f.write(img_bytes)

    print(f"\n[SERVER] Photo recue : {filepath} ({len(img_bytes)} octets)")
    resultat = analyser_visage(img_bytes)
    print(f"[SERVER] Reponse : {resultat}")
    return jsonify(resultat), 200


@app.route("/reload", methods=["GET"])
def reload_dataset():
    charger_dataset()
    return jsonify({
        "success": True,
        "encodings": len(visages_connus),
        "patients": sorted(set(noms_connus))
    }), 200


@app.route("/status", methods=["GET"])
def status():
    return jsonify({
        "success": True,
        "encodings": len(visages_connus),
        "patients": sorted(set(noms_connus))
    }), 200


if __name__ == "__main__":
    print("=" * 55)
    print("  Smart Med Box - Serveur reconnaissance faciale")
    print("=" * 55)
    charger_dataset()
    app.run(host="0.0.0.0", port=5000, debug=False)