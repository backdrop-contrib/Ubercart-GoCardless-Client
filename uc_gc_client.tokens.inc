<?php

/**
 * @file
 * Defines a token for use by modules.
 */

/**
 * Implements hook_token_info().
 */
function uc_gc_client_token_info() {
  // Add tokens.
  $uc_order['order-total-int'] = array(
    'name' => t('Order total international'),
    'description' => t('Formatted string including currency sign and order total amount.'),
  );
  return array(
    'tokens' => array(
      'uc_order' => $uc_order,
    ),
  );
}

/**
 * Implements hook_tokens().
 */
function uc_gc_client_tokens($type, $tokens, $data = array(), $options = array()) {
  $replacements = array();
  if ($type == 'uc_order' && !empty($data['uc_order'])) {
    $order = $data['uc_order'];
    foreach ($tokens as $name => $original) {
      switch ($name) {
        case 'order-total-int':
          if (isset($order->data['initial_payment'])) {
            $replacements[$original] = $order->data['initial_payment'];
          }
          else {
            $replacements[$original] = uc_currency_format($order->order_total, variable_get('uc_currency_sign'));
          }
          break;
      }
    }
  }
  return $replacements;
}
