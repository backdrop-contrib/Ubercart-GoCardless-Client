<?php

/**
 * @file
 * Provides definitions and actions for the Rules module.
 */

/**
 * Implements hook_rules_event_info().
 *
 * @ingroup rules
 */
function uc_gc_client_rules_event_info() {
  return array(
    'uc_gc_client_webhook' => array(
      'label' => t('A webhook is received from GoCardless'),
      'group' => t('Payment'),
      'variables' => array(
        'items' => array(
          'type' => 'text',
          'label' => t('Items'),
        ),
        'resource' => array(
          'type' => 'text',
          'label' => t('GC resource that created the webhook'),
        ),
        'order_id' => array(
          'type' => 'text',
          'label' => t('Order ID'),
        ),
      ),
    ),
  );
}

/**
 * Action implementation: cancels a subscription on GoCardless.com.
 */
function uc_gc_client_action_cancel($order) {

  if (isset($order->order_id)) {

    $gc_client = db_select('uc_gc_client', 'c')
      ->fields('c')
      ->condition('ucid', $order->order_id)
      ->execute()->fetch();

    $params = array(
      'endpoint' => 'mandates',
      'action' => 'cancel',
      'mandate' => $gc_client->gcid,
    );
    $result = uc_gc_client_api($params);
  }
}
