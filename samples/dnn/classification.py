import os
import glob
import argparse
import cv2 as cv
import numpy as np
import sys
from common import *

def help():
    print(
        '''
        Firstly, download required models using `download_models.py` (if not already done). Set environment variable OPENCV_DOWNLOAD_CACHE_DIR to specify where models should be downloaded. Also, point OPENCV_SAMPLES_DATA_PATH to opencv/samples/data.\n"\n

        To run:
            python classification.py model_name --input=path/to/your/input/image/or/video (don't give --input flag if want to use device camera)

        Sample command:
            python classification.py googlenet --input=path/to/image
        Model path can also be specified using --model argument
        '''
    )

def get_args_parser(func_args):
    backends = ("default", "openvino", "opencv", "vkcom", "cuda")
    targets = ("cpu", "opencl", "opencl_fp16", "ncs2_vpu", "hddl_vpu", "vulkan", "cuda", "cuda_fp16")

    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--zoo', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'models.yml'),
                        help='An optional path to file with preprocessing parameters.')
    parser.add_argument('--input',
                        help='Path to input image or video file. Skip this argument to capture frames from a camera.')
    parser.add_argument('--crop', type=bool, default=False,
                        help='Center crop the image.')
    parser.add_argument('--backend', default="default", type=str, choices=backends,
                    help="Choose one of computation backends: "
                         "default: automatically (by default), "
                         "openvino: Intel's Deep Learning Inference Engine (https://software.intel.com/openvino-toolkit), "
                         "opencv: OpenCV implementation, "
                         "vkcom: VKCOM, "
                         "cuda: CUDA, "
                         "webnn: WebNN")
    parser.add_argument('--target', default="cpu", type=str, choices=targets,
                    help="Choose one of target computation devices: "
                         "cpu: CPU target (by default), "
                         "opencl: OpenCL, "
                         "opencl_fp16: OpenCL fp16 (half-float precision), "
                         "ncs2_vpu: NCS2 VPU, "
                         "hddl_vpu: HDDL VPU, "
                         "vulkan: Vulkan, "
                         "cuda: CUDA, "
                         "cuda_fp16: CUDA fp16 (half-float preprocess)")


    args, _ = parser.parse_known_args()
    add_preproc_args(args.zoo, parser, 'classification')
    parser = argparse.ArgumentParser(parents=[parser],
                                     description='Use this script to run classification deep learning networks using OpenCV.',
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    return parser.parse_args(func_args)

def load_images(directory):
    # List all common image file extensions, feel free to add more if needed
    extensions = ['jpg', 'jpeg', 'png', 'bmp', 'tif', 'tiff']
    files = []
    for extension in extensions:
        files.extend(glob.glob(os.path.join(directory, f'*.{extension}')))
    return files

def main(func_args=None):
    args = get_args_parser(func_args)
    if args.alias is None or hasattr(args, 'help'):
        help()
        exit(1)

    args.model = findModel(args.model, args.sha1)
    args.labels = findFile(args.labels)

    # Load names of classes
    labels = None
    if args.labels:
        with open(args.labels, 'rt') as f:
            labels = f.read().rstrip('\n').split('\n')

    # Load a network
    engine = cv.dnn.ENGINE_AUTO
    if args.backend != "default" or args.target != "cpu":
        engine = cv.dnn.ENGINE_CLASSIC
    net = cv.dnn.readNetFromONNX(args.model, engine)
    net.setPreferableBackend(get_backend_id(args.backend))
    net.setPreferableTarget(get_target_id(args.target))

    winName = 'Deep learning image classification in OpenCV'
    cv.namedWindow(winName, cv.WINDOW_NORMAL)

    isdir = False

    if args.input:
        input_path = args.input

        if os.path.isdir(input_path):
            isdir = True
            image_files = load_images(input_path)
            if not image_files:
                print("No images found in the directory.")
                exit(-1)
            current_image_index = 0
        else:
            input_path = findFile(input_path)
            cap = cv.VideoCapture(input_path)
            if not cap.isOpened():
                print("Failed to open the input video")
                exit(-1)
    else:
        cap = cv.VideoCapture(0)

    while cv.waitKey(1) < 0:
        if isdir:
            if current_image_index >= len(image_files):
                break
            frame = cv.imread(image_files[current_image_index])
            current_image_index += 1
        else:
            hasFrame, frame = cap.read()
            if not hasFrame:
                cv.waitKey()
                break

        # Create a 4D blob from a frame.
        inpWidth = args.width if args.width else frame.shape[1]
        inpHeight = args.height if args.height else frame.shape[0]

        blob = cv.dnn.blobFromImage(frame, args.scale, (inpWidth, inpHeight), args.mean, args.rgb, crop=args.crop)
        if args.std:
            blob[0] /= np.asarray(args.std, dtype=np.float32).reshape(3, 1, 1)

        # Run a model
        net.setInput(blob)
        out = net.forward()

        (h, w, _) = frame.shape
        roi_rows = min(300, h)
        roi_cols = min(1000, w)
        frame[:roi_rows,:roi_cols,:] >>= 1

        # Put efficiency information.
        t, _ = net.getPerfProfile()
        label = 'Inference time: %.1f ms' % (t * 1000.0 / cv.getTickFrequency())
        cv.putText(frame, label, (15, 30), cv.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0))

        # Print predicted classes.
        out = out.flatten()
        K = 5
        topKidx = np.argpartition(out, -K)[-K:]
        for i in range(K):
            classId = topKidx[i]
            confidence = out[classId]
            label = '%s: %.2f' % (labels[classId] if labels else 'Class #%d' % classId, confidence)
            cv.putText(frame, label, (15, 90 + i*30), cv.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0))

        cv.imshow(winName, frame)
        key = cv.waitKey(1000 if isdir else 100)

        if key >= 0:
            key &= 255
            if key == ord(' '):
                key = cv.waitKey() & 255
            if key == ord('q') or key == 27:  # Wait for 1 second on each image, press 'q' to exit
                sys.exit(0)
    cv.waitKey()

if __name__ == "__main__":
    main()