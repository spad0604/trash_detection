from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch_ros.actions import Node
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    default_config_file = PathJoinSubstitution(
        [FindPackageShare("trash_sorting_ros"), "config", "pipeline.yaml"]
    )
    config_file = LaunchConfiguration("config")

    common = {"parameters": [config_file], "output": "screen"}
    return LaunchDescription(
        [
            DeclareLaunchArgument("config", default_value=default_config_file),
            Node(package="trash_sorting_ros", executable="sensor_bridge", name="sensor_bridge", **common),
            Node(package="trash_sorting_ros", executable="actuator_bridge", name="actuator_bridge", **common),
            Node(package="trash_sorting_ros", executable="yolo_classifier", name="yolo_classifier", **common),
            Node(package="trash_sorting_ros", executable="trash_orchestrator", name="trash_orchestrator", **common),
            Node(package="trash_sorting_ros", executable="firebase_bridge", name="firebase_bridge", **common),
        ]
    )
