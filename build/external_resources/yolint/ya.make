RESOURCES_LIBRARY()



IF (HOST_OS_LINUX)
    DECLARE_EXTERNAL_RESOURCE(YOLINT sbr:1172025912)
    DECLARE_EXTERNAL_RESOURCE(YOLINT_NEXT sbr:1194462998)
ELSEIF (HOST_OS_DARWIN)
    DECLARE_EXTERNAL_RESOURCE(YOLINT sbr:1172029928)
    DECLARE_EXTERNAL_RESOURCE(YOLINT_NEXT sbr:1194462879)
ELSEIF (HOST_OS_WINDOWS)
    DECLARE_EXTERNAL_RESOURCE(YOLINT sbr:1172028989)
    DECLARE_EXTERNAL_RESOURCE(YOLINT_NEXT sbr:1194463428)
ELSE()
    MESSAGE(FATAL_ERROR Unsupported host platform)
ENDIF()

END()
