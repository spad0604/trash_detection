import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';

import '../theme/app_colors.dart';
import '../data/models/trash_bin_model.dart';

class AlertBanner extends StatelessWidget {
  final TrashBinModel bin;
  final VoidCallback onDismiss;

  const AlertBanner({super.key, required this.bin, required this.onDismiss});

  @override
  Widget build(BuildContext context) {
    final alerts = <_AlertItem>[];

    if (bin.fireRisk) {
      alerts.add(
        _AlertItem(
          icon: Icons.local_fire_department_rounded,
          text: 'Phát hiện nguy cơ CHÁY NỔ!',
          color: AppColors.danger,
        ),
      );
    }
    if (bin.gasLeak) {
      alerts.add(
        _AlertItem(
          icon: Icons.air_rounded,
          text: 'Phát hiện rò rỉ khí gas!',
          color: AppColors.warning,
        ),
      );
    }
    if (bin.bin1Full) {
      alerts.add(
        _AlertItem(
          icon: Icons.delete_rounded,
          text: 'Ngăn Tái chế đã đầy!',
          color: AppColors.binPlastic,
        ),
      );
    }
    if (bin.bin2Full) {
      alerts.add(
        _AlertItem(
          icon: Icons.delete_rounded,
          text: 'Ngăn Hữu cơ đã đầy!',
          color: AppColors.binOrganic,
        ),
      );
    }
    if (bin.bin3Full) {
      alerts.add(
        _AlertItem(
          icon: Icons.delete_rounded,
          text: 'Ngăn Khác đã đầy!',
          color: AppColors.binOther,
        ),
      );
    }

    if (alerts.isEmpty) return const SizedBox.shrink();

    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: AppColors.danger.withOpacity(0.08),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: AppColors.danger.withOpacity(0.3), width: 1),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Icon(
                Icons.warning_rounded,
                size: 18,
                color: AppColors.danger,
              ),
              const SizedBox(width: 8),
              Text(
                'CẢNH BÁO KHẨN CẤP',
                style: GoogleFonts.inter(
                  fontSize: 13,
                  fontWeight: FontWeight.w700,
                  color: AppColors.danger,
                  letterSpacing: 0.5,
                ),
              ),
              const Spacer(),
              InkWell(
                onTap: onDismiss,
                borderRadius: BorderRadius.circular(8),
                child: Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 10,
                    vertical: 4,
                  ),
                  decoration: BoxDecoration(
                    color: AppColors.danger.withOpacity(0.15),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Text(
                    'Xóa tất cả',
                    style: GoogleFonts.inter(
                      fontSize: 11,
                      fontWeight: FontWeight.w600,
                      color: AppColors.danger,
                    ),
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 10),
          ...alerts.map(
            (a) => Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Row(
                children: [
                  Icon(a.icon, size: 16, color: a.color),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      a.text,
                      style: GoogleFonts.inter(
                        fontSize: 13,
                        fontWeight: FontWeight.w500,
                        color: AppColors.textPrimary,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _AlertItem {
  final IconData icon;
  final String text;
  final Color color;

  _AlertItem({required this.icon, required this.text, required this.color});
}
