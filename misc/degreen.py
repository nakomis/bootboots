import cv2
import numpy as np

def increase_brightness(img, value=30):
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    h, s, v = cv2.split(hsv)

    lim = 255 - value
    v[v > lim] = 255
    v[v <= lim] += value

    final_hsv = cv2.merge((h, s, v))
    img = cv2.cvtColor(final_hsv, cv2.COLOR_HSV2BGR)
    return img

img = cv2.imread('/Users/martinharris/repos/nakomis/bootboot/mqtt/MQTT.js/receivedimages/35ed59da-44de-4d93-939f-6081b21cf9c2.jpg')
# percent = 0.5
percent = 0.4
#percent = 0

# convert to HSV
hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
h,s,v = cv2.split(hsv)

# desaturate
s_desat = cv2.multiply(s, percent).astype(np.uint8)
# h_sat = cv2.multiply(h, percent).astype(np.uint8)
# v_sat = cv2.multiply(v, percent).astype(np.uint8)
hsv_new = cv2.merge([h,s_desat,v])
bgr_desat = cv2.cvtColor(hsv_new, cv2.COLOR_HSV2BGR)

# create 1D LUT for green
# (120 out of 360) = (60 out of 180)  +- 25
lut = np.zeros((1,256), dtype=np.uint8)
white = np.full((1,50), 255, dtype=np.uint8)
lut[0:1, 35:85] = white
print(lut.shape, lut.dtype)

# apply lut to hue channel as mask
mask = cv2.LUT(h, lut)
mask = mask.astype(np.float32) / 255
mask = cv2.merge([mask,mask,mask])

# mask bgr_desat and img
result = mask * bgr_desat + (1 - mask)*img
result = result.clip(0,255).astype(np.uint8)

result = increase_brightness(result, 30)

# save result
cv2.imwrite('outimage.jpg', result)
#cv2.imwrite('barn_desat2_0p25.jpg', result)
#cv2.imwrite('barn_desat2_0.jpg', result)

cv2.imshow('img', img)
cv2.imshow('bgr_desat', bgr_desat)
cv2.imshow('result', result)
cv2.waitKey(0)
cv2.destroyAllWindows()
