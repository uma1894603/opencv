function(download_fastcv root_dir)

  # Commit SHA in the opencv_3rdparty repo
  set(FASTCV_COMMIT "65f40fc8f7a6aac44936ae9538e69edede6c4b15")

  # Define actual FCV versions
  if(ANDROID)
    if(AARCH64)
      set(FCV_PACKAGE_NAME  "fastcv_android_aarch64_2024_10_24.tgz")
      set(FCV_PACKAGE_HASH  "8a259eea80064643bad20f72ba0b6066")
    else()
      set(FCV_PACKAGE_NAME  "fastcv_android_arm32_2024_10_24.tgz")
      set(FCV_PACKAGE_HASH  "04d89219c44d54166b2b7f8c0ed5143b")
    endif()
  elseif(UNIX AND NOT APPLE AND NOT IOS AND NOT XROS)
    message("FastCV: fastcv lib for Linux is not supported for now!")
  endif(ANDROID)

  # Download Package
  set(OPENCV_FASTCV_URL "https://raw.githubusercontent.com/opencv/opencv_3rdparty/${FASTCV_COMMIT}/fastcv/")

  ocv_download( FILENAME        ${FCV_PACKAGE_NAME}
                HASH            ${FCV_PACKAGE_HASH}
                URL             ${OPENCV_FASTCV_URL}
                DESTINATION_DIR ${root_dir}
                ID              FASTCV
                STATUS          res
                UNPACK
                RELATIVE_URL)

  if(NOT res)
    message("FastCV: package download failed! Please download FastCV manually and put it at ${root_dir}.")
  endif()

endfunction()