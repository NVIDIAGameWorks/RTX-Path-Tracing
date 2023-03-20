
import json
import argparse
import os.path

parser = argparse.ArgumentParser(description = ".mat.json to .gltf converter")
parser.add_argument('--mat', '-m', required = True, help = "Input material file")
parser.add_argument('--gltf', '-g', required = True, help = "Input glTF file")
parser.add_argument('--out', '-o', required = True, help = "Output glTF file")
parser.add_argument('--spec-gloss', '-s', action = "store_true", help = "Input materials are in specular-gloss format")


args = parser.parse_args()

with open(args.mat, "r") as matfile:
	mat = json.load(matfile)

with open(args.gltf, "r") as gltfile:
	gltf = json.load(gltfile)


anyDdsTextures = False

if "extensionsUsed" in gltf:
	extensionsUsed = gltf["extensionsUsed"]
else:
	extensionsUsed = []
	gltf["extensionsUsed"] = extensionsUsed

if "textures" in gltf:
	textures = gltf["textures"]
else:
	textures = []
	gltf["textures"] = textures

if "images" in gltf:
	images = gltf["images"]
else:
	images = []
	gltf["images"] = images

def add_image(path):
	imageIndex = 0
	for image in images:
		if image["uri"] == path:
			return imageIndex
		imageIndex += 1
	image = { "uri": path }
	images.append(image)
	return imageIndex

def add_texture(path):
	if path.lower().endswith(".dds"):
		global anyDdsTextures
		anyDdsTextures = True

		testPath = path[:-4] + ".png"
		if os.path.exists(testPath):
			regularImage = add_image(testPath)
		else:
			testPath = path[:-4] + ".jpg"
			if os.path.exists(testPath):
				regularImage = add_image(testPath)
			else:
				print("WARNING: non-DDS texture not found for %s", path)
				regularImage = None

		ddsImage = add_image(path)
	else:
		regularImage = add_image(path)
		ddsImage = None

	textureIndex = 0
	for texture in textures:
		if texture["source"] == regularImage \
		or ("extensions" in texture) and texture["extensions"]["MSFT_texture_dds"]["source"] == ddsImage:
			return textureIndex
		textureIndex += 1

	texture = { }
	if regularImage is not None:
		texture["source"] = regularImage
	if ddsImage is not None:
		texture["extensions"] = { "MSFT_texture_dds": { "source": ddsImage } }
	textures.append(texture)
	return textureIndex

	
newmatlist = []
for matnode in gltf["materials"]:
	try:
		matdef = mat[matnode["name"]]
		matnode = { "name": matnode["name"] }
		newmatlist.append(matnode)
	except KeyError:
		print("Definition for %s not found", matnode.name)
		newmatlist.append(matnode)
		continue

	if matdef.get("AlphaTested", False):
		matnode["alphaMode"] = "MASK"
		matnode["doubleSided"] = True
	elif matdef.get("Transparent", False):
		matnode["alphaMode"] = "BLEND"
		matnode["doubleSided"] = True
	else:
		matnode["alphaMode"] = "OPAQUE"

	emittance = matdef.get("Emittance", [])
	if any(emittance) and len(emittance) == 3:
		matnode["emissiveFactor"] = emittance

	if args.spec_gloss:
		specGloss = {}
		matnode["extensions"] = { "KHR_materials_pbrSpecularGlossiness": specGloss }
	else:
		metalRough = {}
		matnode["pbrMetallicRoughness"] = metalRough

	texdefs = matdef.get("Textures", {})
	if texdefs:
		emissiveTexture = texdefs.get("Emittance")
		if emissiveTexture:
			matnode["emissiveTexture"] = { "index": add_texture(emissiveTexture) }

		normalTexture = texdefs.get("Bumpmap")
		if normalTexture:
			matnode["normalTexture"] = { "index": add_texture(normalTexture) }

		diffuseTexture = texdefs.get("Diffuse")
		if diffuseTexture:
			if args.spec_gloss:
				specGloss["diffuseTexture"] = { "index": add_texture(diffuseTexture) }
			else:
				metalRough["baseColorTexture"] = { "index": add_texture(diffuseTexture) }

		specularTexture = texdefs.get("Specular")
		if specularTexture:
			if args.spec_gloss:
				specGloss["specularGlossinessTexture"] = { "index": add_texture(specularTexture) }
			else:
				metalRough["metallicRoughnessTexture"] = { "index": add_texture(specularTexture) }
	else:
		diffuseTexture = None
		specularTexture = None

	if args.spec_gloss:
		if not diffuseTexture:
			diffuseFactor = matdef.get("Diffuse")
			opacity = matdef.get("Opacity", 1.0)
			if len(diffuseFactor) == 3:
				diffuseFactor.append(opacity)
				specGloss["diffuseFactor"] = diffuseFactor

		if not specularTexture:
			specularFactor = matdef.get("Specular")
			if len(specularFactor) == 3:
				specGloss["specularFactor"] = specularFactor

			specGloss["glossinessFactor"] = matdef.get("Shininess")
	else:
		if not diffuseTexture:
			diffuseFactor = matdef.get("Diffuse")
			opacity = matdef.get("Opacity", 1.0)
			if len(diffuseFactor) == 3:
				diffuseFactor.append(opacity)
				metalRough["baseColorFactor"] = diffuseFactor

		if not specularTexture:
			specularFactor = matdef.get("Specular")
			if len(specularFactor) == 3:
				metallicFactor = 0
				for i in range(3):
					metallicFactor += specularFactor[i] / (specularFactor[i] + diffuseFactor[i])
				metallicFactor /= 3.0
				metalRough["metallicFactor"] = metallicFactor

			metalRough["roughnessFactor"] = 1.0 - matdef.get("Shininess")



gltf["materials"] = newmatlist

if args.spec_gloss:
	extensionsUsed.append("KHR_materials_pbrSpecularGlossiness")
if anyDdsTextures:
	extensionsUsed.append("MSFT_texture_dds")


with open(args.out, "w") as outfile:
	outfile.write(json.dumps(gltf, indent = 4))
