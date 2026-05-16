import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';

import '../theme/app_colors.dart';

class StatusBadge extends StatelessWidget {
  final String label;
  final bool isOnline;
  final bool hasAlert;

  const StatusBadge({
    super.key,
    required this.label,
    required this.isOnline,
    this.hasAlert = false,
  });

  @override
  Widget build(BuildContext context) {
    final Color dotColor;
    final Color bgColor;

    if (hasAlert) {
      dotColor = AppColors.danger;
      bgColor = AppColors.danger.withOpacity(0.12);
    } else if (isOnline) {
      dotColor = AppColors.success;
      bgColor = AppColors.success.withOpacity(0.12);
    } else {
      dotColor = AppColors.textMuted;
      bgColor = AppColors.surfaceAlt;
    }

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
      decoration: BoxDecoration(
        color: bgColor,
        borderRadius: BorderRadius.circular(20),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 7,
            height: 7,
            decoration: BoxDecoration(
              color: dotColor,
              shape: BoxShape.circle,
            ),
          ),
          const SizedBox(width: 6),
          Text(
            label,
            style: GoogleFonts.inter(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: dotColor,
            ),
          ),
        ],
      ),
    );
  }
}
