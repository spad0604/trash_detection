import 'package:flutter/material.dart';
import 'package:get/get.dart';
import '../../../data/models/trash_bin_model.dart';
import '../../../data/repositories/trash_bin_repository.dart';

class DashboardController extends GetxController {
  final TrashBinRepository _repository = TrashBinRepository();

  final Rx<TrashBinModel?> currentBin = Rx<TrashBinModel?>(null);
  final RxList<TrashBinModel> allBins = <TrashBinModel>[].obs;
  final RxBool isLoading = true.obs;
  final RxString selectedBinId = 'bin_001'.obs;
  final TextEditingController smsPhoneController = TextEditingController();

  @override
  void onInit() {
    super.onInit();
    _listenToBin(selectedBinId.value);
    _listenToAllBins();
  }

  void _listenToBin(String binId) {
    _repository
        .streamBin(binId)
        .listen(
          (bin) {
            currentBin.value = bin;
            if (smsPhoneController.text.trim().isEmpty &&
                bin.notificationPhoneNumber.isNotEmpty) {
              smsPhoneController.text = bin.notificationPhoneNumber;
            }
            isLoading.value = false;
          },
          onError: (e) {
            debugPrint('[Dashboard] Stream error: $e');
            isLoading.value = false;
          },
        );
  }

  void _listenToAllBins() {
    _repository.streamAllBins().listen((bins) {
      allBins.assignAll(bins);
    });
  }

  void selectBin(String binId) {
    selectedBinId.value = binId;
    isLoading.value = true;
    _listenToBin(binId);
  }

  Future<void> clearAlert(String alertKey) async {
    await _repository.clearAlert(selectedBinId.value, alertKey);
  }

  Future<void> clearAllAlerts() async {
    await _repository.clearAllAlerts(selectedBinId.value);
  }

  Future<void> requestGoDump() async {
    try {
      debugPrint(
        '[DashboardController] requestGoDump selectedBinId=${selectedBinId.value}',
      );
      await _repository.requestGoDump(selectedBinId.value);
      Get.snackbar('Command', 'Da gui lenh di do rac');
    } catch (e) {
      debugPrint('[DashboardController] requestGoDump error: $e');
      Get.snackbar('Firebase error', e.toString());
      rethrow;
    }
  }

  Future<void> requestGoHome() async {
    try {
      debugPrint(
        '[DashboardController] requestGoHome selectedBinId=${selectedBinId.value}',
      );
      await _repository.requestGoHome(selectedBinId.value);
      Get.snackbar('Command', 'Da gui lenh ve nha');
    } catch (e) {
      debugPrint('[DashboardController] requestGoHome error: $e');
      Get.snackbar('Firebase error', e.toString());
      rethrow;
    }
  }

  Future<void> requestStop() async {
    try {
      debugPrint(
        '[DashboardController] requestStop selectedBinId=${selectedBinId.value}',
      );
      await _repository.requestStop(selectedBinId.value);
      Get.snackbar('Command', 'Da gui lenh dung');
    } catch (e) {
      debugPrint('[DashboardController] requestStop error: $e');
      Get.snackbar('Firebase error', e.toString());
      rethrow;
    }
  }

  Future<void> saveNotificationPhoneNumber() async {
    try {
      final phone = smsPhoneController.text.trim();
      await _repository.updateNotificationPhoneNumber(
        selectedBinId.value,
        phone,
      );
      Get.snackbar('SMS', 'Da luu so dien thoai nhan tin');
    } catch (e) {
      debugPrint('[DashboardController] saveNotificationPhoneNumber error: $e');
      Get.snackbar('Firebase error', e.toString());
      rethrow;
    }
  }

  Future<void> requestSendSms(String type) async {
    try {
      await saveNotificationPhoneNumber();
      await _repository.requestSendSms(selectedBinId.value, type);
      Get.snackbar('SMS', 'Da gui yeu cau nhan tin');
    } catch (e) {
      debugPrint('[DashboardController] requestSendSms error: $e');
      Get.snackbar('Firebase error', e.toString());
      rethrow;
    }
  }

  @override
  void onClose() {
    smsPhoneController.dispose();
    super.onClose();
  }

  // ── Computed getters for UI ─────────────────────────────
  String get statusText {
    final bin = currentBin.value;
    if (bin == null) return 'Offline';
    switch (bin.state) {
      case 'idle':
        return 'Sẵn sàng';
      case 'processing':
        return 'Đang xử lý';
      case 'sorting':
        return 'Đang phân loại';
      case 'moving':
      case 'dump_outbound':
        return 'Đang đi đổ rác';
      case 'dump_requested':
        return 'Đã yêu cầu đổ rác';
      case 'awaiting_return':
      case 'dump_completed':
        return 'Đang chờ lệnh về nhà';
      case 'home_requested':
        return 'Đã yêu cầu về nhà';
      case 'dump_returning':
        return 'Đang về nhà';
      case 'home_completed':
        return 'Đã về nhà';
      case 'line_lost':
        return 'Mất line';
      case 'alert':
        return 'Cảnh báo!';
      default:
        return 'Offline';
    }
  }

  bool get hasActiveAlert => currentBin.value?.hasAlert ?? false;
}
