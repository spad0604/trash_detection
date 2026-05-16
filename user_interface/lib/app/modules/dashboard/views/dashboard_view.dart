import 'package:flutter/material.dart';
import 'package:get/get.dart';
import 'package:google_fonts/google_fonts.dart';
import 'package:percent_indicator/circular_percent_indicator.dart';

import '../../../theme/app_colors.dart';
import '../controllers/dashboard_controller.dart';
import '../../../widgets/alert_banner.dart';
import '../../../widgets/battery_indicator.dart';
import '../../../widgets/status_badge.dart';

class DashboardView extends GetView<DashboardController> {
  const DashboardView({super.key});

  @override
  Widget build(BuildContext context) {
    final isWide = MediaQuery.of(context).size.width > 800;

    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: Obx(() {
          if (controller.isLoading.value) {
            return const Center(
              child: CircularProgressIndicator(color: AppColors.primary),
            );
          }

          final bin = controller.currentBin.value;
          if (bin == null) {
            return _buildOfflineState();
          }

          return CustomScrollView(
            slivers: [
              SliverToBoxAdapter(child: _buildHeader()),
              if (bin.hasAlert)
                SliverToBoxAdapter(
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(20, 0, 20, 0),
                    child: AlertBanner(
                      bin: bin,
                      onDismiss: controller.clearAllAlerts,
                    ),
                  ),
                ),
              SliverPadding(
                padding: const EdgeInsets.fromLTRB(20, 0, 20, 20),
                sliver: isWide
                    ? _buildWideContent(bin)
                    : _buildNarrowContent(bin),
              ),
            ],
          );
        }),
      ),
    );
  }

  // ══════════════════════════════════════════════════════════════
  //  HEADER
  // ══════════════════════════════════════════════════════════════
  Widget _buildHeader() {
    final bin = controller.currentBin.value;
    return Container(
      padding: const EdgeInsets.fromLTRB(20, 18, 20, 16),
      child: Row(
        children: [
          Container(
            width: 46,
            height: 46,
            decoration: BoxDecoration(
              color: AppColors.primary,
              borderRadius: BorderRadius.circular(13),
            ),
            child: const Icon(
              Icons.delete_outline_rounded,
              color: Colors.white,
              size: 24,
            ),
          ),
          const SizedBox(width: 14),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Smart Trash Bin',
                  style: GoogleFonts.inter(
                    fontSize: 20,
                    fontWeight: FontWeight.w800,
                    color: AppColors.textPrimary,
                  ),
                ),
                const SizedBox(height: 3),
                Row(
                  children: [
                    StatusBadge(
                      label: controller.statusText,
                      isOnline: bin?.isOnline ?? false,
                      hasAlert: controller.hasActiveAlert,
                    ),
                    if (bin != null) ...[
                      const SizedBox(width: 10),
                      BatteryIndicator(
                        percent: bin.batteryPercent,
                        voltage: bin.batteryVoltage,
                      ),
                    ],
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // ══════════════════════════════════════════════════════════════
  //  WIDE LAYOUT — 3 cards side by side
  // ══════════════════════════════════════════════════════════════
  SliverToBoxAdapter _buildWideContent(bin) {
    return SliverToBoxAdapter(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _sectionLabel('Tình trạng từng ngăn rác'),
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Expanded(
                child: _BinDetailCard(
                  label: 'Nhựa / Lon',
                  index: 1,
                  percent: bin.bin1Percent,
                  temperature: bin.temperature1,
                  humidity: bin.humidity1,
                  mq2: bin.mq2_1,
                  mq135: bin.mq135_1,
                  isFull: bin.bin1Full,
                  color: AppColors.binPlastic,
                  icon: Icons.local_drink_outlined,
                ),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: _BinDetailCard(
                  label: 'Hữu cơ',
                  index: 2,
                  percent: bin.bin2Percent,
                  temperature: bin.temperature2,
                  humidity: bin.humidity2,
                  mq2: bin.mq2_2,
                  mq135: bin.mq135_2,
                  isFull: bin.bin2Full,
                  color: AppColors.binOrganic,
                  icon: Icons.eco_outlined,
                ),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: _BinDetailCard(
                  label: 'Khác',
                  index: 3,
                  percent: bin.bin3Percent,
                  temperature: bin.temperature3,
                  humidity: bin.humidity3,
                  mq2: bin.mq2_3,
                  mq135: bin.mq135_3,
                  isFull: bin.bin3Full,
                  color: AppColors.binOther,
                  icon: Icons.category_outlined,
                ),
              ),
            ],
          ),
          const SizedBox(height: 16),
          _buildSystemStatusCard(bin),
        ],
      ),
    );
  }

  // ══════════════════════════════════════════════════════════════
  //  NARROW LAYOUT — stacked cards
  // ══════════════════════════════════════════════════════════════
  SliverList _buildNarrowContent(bin) {
    return SliverList(
      delegate: SliverChildListDelegate([
        _sectionLabel('Tình trạng từng ngăn rác'),
        _BinDetailCard(
          label: 'Nhựa / Lon',
          index: 1,
          percent: bin.bin1Percent,
          temperature: bin.temperature1,
          humidity: bin.humidity1,
          mq2: bin.mq2_1,
          mq135: bin.mq135_1,
          isFull: bin.bin1Full,
          color: AppColors.binPlastic,
          icon: Icons.local_drink_outlined,
        ),
        const SizedBox(height: 14),
        _BinDetailCard(
          label: 'Hữu cơ',
          index: 2,
          percent: bin.bin2Percent,
          temperature: bin.temperature2,
          humidity: bin.humidity2,
          mq2: bin.mq2_2,
          mq135: bin.mq135_2,
          isFull: bin.bin2Full,
          color: AppColors.binOrganic,
          icon: Icons.eco_outlined,
        ),
        const SizedBox(height: 14),
        _BinDetailCard(
          label: 'Khác',
          index: 3,
          percent: bin.bin3Percent,
          temperature: bin.temperature3,
          humidity: bin.humidity3,
          mq2: bin.mq2_3,
          mq135: bin.mq135_3,
          isFull: bin.bin3Full,
          color: AppColors.binOther,
          icon: Icons.category_outlined,
        ),
        const SizedBox(height: 16),
        _buildSystemStatusCard(bin),
        const SizedBox(height: 32),
      ]),
    );
  }

  // ══════════════════════════════════════════════════════════════
  //  SYSTEM STATUS CARD (global info)
  // ══════════════════════════════════════════════════════════════
  Widget _buildSystemStatusCard(bin) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: AppColors.border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Icon(Icons.info_outline_rounded,
                  size: 17, color: AppColors.textSecondary),
              const SizedBox(width: 8),
              Text(
                'Thông tin hệ thống',
                style: GoogleFonts.inter(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  color: AppColors.textSecondary,
                ),
              ),
            ],
          ),
          const SizedBox(height: 16),
          Wrap(
            spacing: 14,
            runSpacing: 12,
            children: [
              _InfoChip(
                icon: Icons.label_rounded,
                label: 'Phân loại cuối',
                value: bin.lastClassification.isEmpty
                    ? '—'
                    : bin.lastClassification,
                color: AppColors.primary,
              ),
              _InfoChip(
                icon: Icons.power_outlined,
                label: 'Điện áp pin',
                value: '${bin.batteryVoltage.toStringAsFixed(1)} V',
                color: bin.batteryPercent > 20
                    ? AppColors.success
                    : AppColors.danger,
              ),
              _InfoChip(
                icon: Icons.local_fire_department_rounded,
                label: 'Nguy cơ cháy',
                value: bin.fireRisk ? 'CÓ!' : 'Bình thường',
                color: bin.fireRisk ? AppColors.danger : AppColors.success,
              ),
              _InfoChip(
                icon: Icons.air_rounded,
                label: 'Khí gas',
                value: bin.gasLeak ? 'RÒ RỈ!' : 'Bình thường',
                color: bin.gasLeak ? AppColors.warning : AppColors.success,
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _sectionLabel(String text) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Text(
        text,
        style: GoogleFonts.inter(
          fontSize: 13,
          fontWeight: FontWeight.w600,
          color: AppColors.textMuted,
          letterSpacing: 0.4,
        ),
      ),
    );
  }

  // ══════════════════════════════════════════════════════════════
  //  OFFLINE STATE
  // ══════════════════════════════════════════════════════════════
  Widget _buildOfflineState() {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Container(
            width: 80,
            height: 80,
            decoration: BoxDecoration(
              color: AppColors.surfaceAlt,
              borderRadius: BorderRadius.circular(20),
            ),
            child: const Icon(Icons.wifi_off_rounded,
                size: 40, color: AppColors.textMuted),
          ),
          const SizedBox(height: 20),
          Text(
            'Thùng rác đang offline',
            style: GoogleFonts.inter(
              fontSize: 18,
              fontWeight: FontWeight.w600,
              color: AppColors.textPrimary,
            ),
          ),
          const SizedBox(height: 8),
          Text(
            'Đang chờ kết nối từ thiết bị...',
            style: GoogleFonts.inter(
              fontSize: 14,
              color: AppColors.textSecondary,
            ),
          ),
        ],
      ),
    );
  }
}

// ════════════════════════════════════════════════════════════════
//  BIN DETAIL CARD — shows all 4 sensors + fill level per bin
// ════════════════════════════════════════════════════════════════
class _BinDetailCard extends StatelessWidget {
  final String label;
  final int index;
  final int percent;
  final double temperature;
  final double humidity;
  final int mq2;
  final int mq135;
  final bool isFull;
  final Color color;
  final IconData icon;

  const _BinDetailCard({
    required this.label,
    required this.index,
    required this.percent,
    required this.temperature,
    required this.humidity,
    required this.mq2,
    required this.mq135,
    required this.isFull,
    required this.color,
    required this.icon,
  });

  @override
  Widget build(BuildContext context) {
    final bool smokeAlert = mq2 > 800;
    final bool gasAlert = mq135 > 700;

    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: isFull
              ? AppColors.danger.withOpacity(0.5)
              : color.withOpacity(0.25),
          width: isFull ? 1.5 : 1,
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // ── Card header ──────────────────────────────────
          Row(
            children: [
              Container(
                width: 34,
                height: 34,
                decoration: BoxDecoration(
                  color: color.withOpacity(0.12),
                  borderRadius: BorderRadius.circular(10),
                ),
                child: Icon(icon, size: 18, color: color),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      label,
                      style: GoogleFonts.inter(
                        fontSize: 14,
                        fontWeight: FontWeight.w700,
                        color: AppColors.textPrimary,
                      ),
                    ),
                    Text(
                      'Ngăn $index',
                      style: GoogleFonts.inter(
                        fontSize: 11,
                        color: AppColors.textMuted,
                      ),
                    ),
                  ],
                ),
              ),
              if (isFull)
                Container(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 8, vertical: 3),
                  decoration: BoxDecoration(
                    color: AppColors.danger.withOpacity(0.12),
                    borderRadius: BorderRadius.circular(6),
                  ),
                  child: Text(
                    'ĐẦY',
                    style: GoogleFonts.inter(
                      fontSize: 10,
                      fontWeight: FontWeight.w700,
                      color: AppColors.danger,
                    ),
                  ),
                ),
            ],
          ),
          const SizedBox(height: 18),

          // ── Circular fill indicator ──────────────────────
          Center(
            child: CircularPercentIndicator(
              radius: 52,
              lineWidth: 7,
              percent: (percent / 100).clamp(0.0, 1.0),
              center: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    '$percent%',
                    style: GoogleFonts.inter(
                      fontSize: 20,
                      fontWeight: FontWeight.w800,
                      color: AppColors.textPrimary,
                    ),
                  ),
                  Text(
                    'đầy',
                    style: GoogleFonts.inter(
                      fontSize: 11,
                      color: AppColors.textMuted,
                    ),
                  ),
                ],
              ),
              progressColor: isFull ? AppColors.danger : color,
              backgroundColor: color.withOpacity(0.1),
              circularStrokeCap: CircularStrokeCap.round,
              animation: true,
              animationDuration: 900,
            ),
          ),
          const SizedBox(height: 18),

          // ── Divider ──────────────────────────────────────
          const Divider(color: AppColors.border, height: 1),
          const SizedBox(height: 14),

          // ── Sensor rows ──────────────────────────────────
          _SensorRow(
            icon: Icons.thermostat_outlined,
            label: 'Nhiệt độ',
            value: '${temperature.toStringAsFixed(1)} °C',
            color: temperature > 50 ? AppColors.danger : AppColors.textSecondary,
          ),
          const SizedBox(height: 10),
          _SensorRow(
            icon: Icons.water_drop_outlined,
            label: 'Độ ẩm',
            value: '${humidity.toStringAsFixed(1)} %',
            color: AppColors.info,
          ),
          const SizedBox(height: 10),
          _SensorRow(
            icon: Icons.local_fire_department_outlined,
            label: 'Khói (MQ-2)',
            value: mq2.toString(),
            color: smokeAlert ? AppColors.danger : AppColors.success,
            badge: smokeAlert ? 'CẢNH BÁO' : null,
          ),
          const SizedBox(height: 10),
          _SensorRow(
            icon: Icons.air_outlined,
            label: 'Khí (MQ-135)',
            value: mq135.toString(),
            color: gasAlert ? AppColors.warning : AppColors.success,
            badge: gasAlert ? 'CAO' : null,
          ),
        ],
      ),
    );
  }
}

// ════════════════════════════════════════════════════════════════
//  SENSOR ROW ITEM
// ════════════════════════════════════════════════════════════════
class _SensorRow extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final Color color;
  final String? badge;

  const _SensorRow({
    required this.icon,
    required this.label,
    required this.value,
    required this.color,
    this.badge,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 15, color: color),
        const SizedBox(width: 8),
        Text(
          label,
          style: GoogleFonts.inter(
            fontSize: 12,
            color: AppColors.textSecondary,
          ),
        ),
        const Spacer(),
        if (badge != null) ...[
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
            margin: const EdgeInsets.only(right: 6),
            decoration: BoxDecoration(
              color: color.withOpacity(0.12),
              borderRadius: BorderRadius.circular(4),
            ),
            child: Text(
              badge!,
              style: GoogleFonts.inter(
                fontSize: 9,
                fontWeight: FontWeight.w700,
                color: color,
              ),
            ),
          ),
        ],
        Text(
          value,
          style: GoogleFonts.inter(
            fontSize: 13,
            fontWeight: FontWeight.w700,
            color: AppColors.textPrimary,
          ),
        ),
      ],
    );
  }
}

// ════════════════════════════════════════════════════════════════
//  INFO CHIP for system status
// ════════════════════════════════════════════════════════════════
class _InfoChip extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final Color color;

  const _InfoChip({
    required this.icon,
    required this.label,
    required this.value,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      decoration: BoxDecoration(
        color: AppColors.surfaceAlt,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: AppColors.border),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 16, color: color),
          const SizedBox(width: 8),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                label,
                style: GoogleFonts.inter(
                  fontSize: 10,
                  color: AppColors.textMuted,
                ),
              ),
              Text(
                value,
                style: GoogleFonts.inter(
                  fontSize: 13,
                  fontWeight: FontWeight.w700,
                  color: color,
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
