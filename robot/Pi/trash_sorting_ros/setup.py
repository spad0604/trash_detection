from setuptools import setup

package_name = "trash_sorting_ros"

setup(
    name=package_name,
    version="0.1.0",
    packages=[package_name],
    data_files=[
        ("share/ament_index/resource_index/packages", [f"resource/{package_name}"]),
        (f"share/{package_name}", ["package.xml"]),
        (f"share/{package_name}/launch", ["launch/bringup.launch.py"]),
        (f"share/{package_name}/config", ["config/pipeline.yaml"]),
        (f"share/{package_name}/models", ["models/best_model.pt"]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="trash_detection",
    maintainer_email="dev@example.com",
    description="Raspberry Pi ROS2 controller for smart trash sorting.",
    license="MIT",
    entry_points={
        "console_scripts": [
            "sensor_bridge = trash_sorting_ros.sensor_bridge:main",
            "actuator_bridge = trash_sorting_ros.actuator_bridge:main",
            "trash_orchestrator = trash_sorting_ros.trash_orchestrator:main",
            "firebase_bridge = trash_sorting_ros.firebase_bridge:main",
            "yolo_classifier = trash_sorting_ros.yolo_classifier:main",
        ],
    },
)
