<?php

/**
 * @file
 * Provides an interface for managing customer's GoCardless mandates.
 */

/**
 * Form constructor for the GoCardless Mandate form.
 *
 * @param object $order
 *   An Ubercart order.
 */
function uc_gc_client_payments_form($form, &$form_state, $order) {
  $gcs = db_select('uc_gc_client', 'g')
    ->fields('g')
    ->condition('ucid', $order->order_id)
    ->execute()->fetchAllAssoc('ucpid');
  $count = count($gcs);
  if ($count == 0) {
    $no_gcs = 'You do not have any recurring payments associated with this order.';
    if ($order->payment_method == 'gc_client') {
      $no_gcs .= ' Check the "Payments" tab for details of Instant Payments.';
    }
    $form['no_orders'] = array(
      '#type' => 'item',
      '#markup' => $no_gcs,
    );
    return $form;
  }

  // Create an array of unique product names.
  $rows = array();
  $product_select = array();
  $title_count = array();
  foreach ($order->products as $product_id => $product) {
    !isset($title_count[$product->title]) ? $title_count[$product->title] = 1 : $title_count[$product->title]++;
    $title_count[$product->title] == 1 ? $title[$product_id] = $product->title : $title[$product_id] = $product->title . ' (' . $title_count[$product->title] . ')';
    $product_select[$product_id] = $title[$product_id];
  }
  if ($count == 1) {
    $ucpid = reset($gcs)->ucpid;
    $text_top = '<h3><b>' . t('Administrate <a href = "@url">@title</a>', array(
      '@title' => $product_select[$ucpid],
      '@url' => url('admin/store/orders/' . $order->order_id),
    )) . '</b></h3>';
    $form['text_top'] = array(
      '#type' => 'item',
      '#markup' => $text_top,
    );
    $form['ucpid'] = array(
      '#type' => 'value',
      '#value' => $ucpid,
    );
  }
  else {
    if ($form_state['input']) {
      $ucpid = $form_state['input']['ucpid'];
    }
    elseif (isset($_SESSION['uc_gc_client_change_ucpid_' . $order->order_id])) {
      $ucpid = $_SESSION['uc_gc_client_change_ucpid_' . $order->order_id]['ucpid'];
    }
    else {
      $ucpid = reset($gcs)->ucpid;
    }
    $form = array(
      '#prefix' => '<div id="change-product">',
      '#suffix' => '</div>',
    );
    $text_top = '<h3><b>' . t('Administrate <a href = "@url">@title</a>', array(
      '@title' => $product_select[$ucpid],
      '@url' => url('admin/store/orders/' . $order->order_id),
    )) . '</b></h3>';

    $form['text_top'] = array(
      '#type' => 'item',
      '#markup' => $text_top,
    );
    $form['ucpid'] = array(
      '#title' => t('Switch product'),
      '#type' => 'select',
      '#options' => $product_select,
      '#default_value' => $ucpid,
      '#ajax' => array(
        'callback' => 'uc_gc_client_payments_form_callback',
        'wrapper' => 'change-product',
        'method' => 'replace',
      ),
    );
  }

  $gc = $gcs[$ucpid];
  $params = array(
    'endpoint' => 'mandates',
    'action' => 'get',
    'mandate' => $gc->gcid,
  );
  $result = uc_gc_client_api($params);
  if (!isset($result->response)) {
    backdrop_set_message(t('No GoCardless mandate found for this order'));
    return $form;
  }

  $mandate = $result->response->body->mandates;
  foreach (uc_gc_client_countries_define() as $gc_country) {
    if ($gc_country['region'] == $mandate->scheme) {
      $sign = $gc_country['sign'];
      break;
    }
  }
  $product = $order->products[$ucpid];

  $interval = $product->data['interval_params']['length'] . ' ' . $product->data['interval_params']['unit'];

  if ($gc->type == 'P') {
    // Payments.
    $type = 'One-off payments';
    $header = array(
      t('Product'),
      t('Payment type'),
      t('Cost'),
      t('Price'),
      t('Payment interval'),
      t('Next scheduled payment creation'),
      t('Next possible charge date'),
      t('Gocardless Id'),
      t('Created'),
      t('Status'),
    );

    $payment_header = array(
      t('Created at'),
      t('Payment name'),
      t('Payment ID'),
      t('Amount'),
      t('Status'),
      t('Charge customer at'),
      '',
    );

    $rows[$product->order_product_id] = array(
      $product_select[$ucpid],
      $type,
      uc_currency_format($product->cost * $product->qty),
      uc_currency_format($product->price * $product->qty),
      $interval == ' ' ? t('Not set') : $interval,
      $mandate->status != 'cancelled' && !is_null($gc->next_payment) ? format_date($gc->next_payment, 'medium') : '-',
      !is_null($mandate->next_possible_charge_date) ? format_date(strtotime($mandate->next_possible_charge_date), 'uc_store') : '-',
      check_plain($mandate->id),
      format_date(strtotime($mandate->created_at), 'uc_store'),
      check_plain($mandate->status),
    );
    $params = array(
      'endpoint' => 'payments',
      'action' => 'list',
      'mandate' => $gc->gcid,
      'limit' => 500,
    );
    $result = uc_gc_client_api($params);
    $payments = $result->response->body->payments;
  }
  else {
    // Subscriptions.
    $type = 'Subscription';
    $header = array(
      t('product'),
      t('payment type'),
      t('cost'),
      t('price'),
      t('payment interval'),
      t('next possible charge date'),
      t('gocardless id'),
      t('created'),
      t('status'),
    );

    $payment_header = array(
      t('Payment name'),
      t('Payment ID'),
      t('Amount'),
      t('Created at'),
      t('Status'),
      t('Charge customer at'),
    );

    $rows[$product->order_product_id] = array(
      $product_select[$ucpid],
      $type,
      uc_currency_format($product->cost),
      uc_currency_format($product->price),
      $interval,
      format_date(strtotime($mandate->next_possible_charge_date), 'uc_store'),
      check_plain($mandate->id),
      format_date(strtotime($mandate->created_at), 'uc_store'),
      check_plain($mandate->status),
    );

    $params = array(
      'endpoint' => 'subscriptions',
      'action' => 'list',
      'mandate' => $gc->gcid,
    );
    $result = uc_gc_client_api($params);
    $subscriptions = $result->response->body->subscriptions;

    foreach ($subscriptions as $subscription) {
      if (isset($subscription->metadata->ucpid)) {
        if ($subscription->metadata->ucpid == $ucpid) {
          $subscription_id = $subscription->id;
          break;
        }
      }
    }

    $params = array(
      'endpoint' => 'payments',
      'action' => 'list',
      'subscription' => $subscription_id,
      'limit' => 500,
    );
    $result = uc_gc_client_api($params);
    $payments = $result->response->body->payments;

    // One-off payments that have been created under the same mandate.
    $params = array(
      'endpoint' => 'payments',
      'action' => 'list',
      'mandate' => $gc->gcid,
      'limit' => 500,
    );
    $result = uc_gc_client_api($params);
    $payments_of = $result->response->body->payments;
    if (count($payments_of) >= 1) {
      $payments = array_merge($payments, $payments_of);
      usort($payments, "uc_gc_client_sort_function");
    }
  }

  $payment_rows = array();
  $payment_total = 0;
  $payment_statuses = array('confirmed', 'paid_out');

  foreach ($payments as &$payment) {
    if (!isset($payment->metadata->ucpid) || $payment->metadata->ucpid != $ucpid) {
      continue;
    }
    if ($payment->status == 'pending_submission') {
      $link = l(t('Cancel'), '/admin/store/orders/' . $order->order_id . '/gocardless/cancel_payment/' . check_plain($payment->id));
    }
    else {
      $link = NULL;
    }

    $payment_rows[] = array(
      format_date(strtotime($payment->created_at), 'uc_store'),
      check_plain($payment->description),
      check_plain($payment->id),
      uc_currency_format($payment->amount / 100, $sign),
      check_plain($payment->status),
      format_date(strtotime($payment->charge_date), 'uc_store'),
      $link,
    );
    if (in_array($payment->status, $payment_statuses)) {
      $payment_total = $payment_total + $payment->amount;
    }
  }
  $form['table'] = array(
    '#theme' => 'table',
    '#header' => $header,
    '#rows' => isset($rows) ? $rows : NULL,
    '#empty' => t('There are no GoCardless payments for this order'),
  );

  if (isset($payment_rows)) {

    $title = t('Payments for @title', array(
      '@title' => $product_select[$ucpid],
    ));
    $form['payment_tables'] = array(
      '#type' => 'fieldset',
      '#title' => check_plain($title),
      '#collapsible' => FALSE,
      '#collapsed' => FALSE,
    );
    $form['payment_tables']['payments_table'] = array(
      '#theme' => 'table',
      '#header' => $payment_header,
      '#rows' => isset($payment_rows) ? $payment_rows : NULL,
      '#empty' => t('There are no payments for this product yet.'),
      '#suffix' => t('Total confirmed payments: @payment_total', array(
        '@payment_total' => uc_currency_format($payment_total / 100, $sign),
      )),
    );
  }

  $form['order_id'] = array(
    '#type' => 'value',
    '#value' => $order->order_id,
  );
  $form['mandate'] = array(
    '#type' => 'value',
    '#value' => $mandate,
  );
  $form['product_select'] = array(
    '#type' => 'value',
    '#value' => $product_select,
  );

  // Create payment section.
  $form['create_payment'] = array(
    '#type' => 'fieldset',
    '#title' => t('Create a payment'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );
  $form['create_payment']['payment_amount'] = array(
    '#type' => 'uc_price',
    '#title' => t('Amount'),
    '#description' => t('Provide amount in default currency. It the order was created using a foreign currency then the amount will be automatically recalculated using the latest exchange rates provided by GoCardless.'),
  );
  $form['create_payment']['payment_title'] = array(
    '#type' => 'textfield',
    '#title' => t('Payment title'),
  );
  $form['create_payment']['charge_date'] = array(
    '#title' => t('Charge customer at'),
    '#type' => 'date_popup',
    '#date_format' => 'd M Y',
    '#default_value' => date('Y-m-d', strtotime($mandate->next_possible_charge_date)),
    '#date_year_range' => '0:+2',
    '#datepicker_options' => array('minDate' => 0),
  );
  $form['create_payment']['payment_submit'] = array(
    '#type' => 'submit',
    '#value' => t('Create a payment with GoCardless'),
    '#validate' => array('uc_gc_client_create_payment_validate'),
    '#submit' => array('uc_gc_client_create_payment_submit'),
  );

  // Generate data for scheduled adjustments table.
  $query = db_select('uc_gc_client_schedules', 's');
  $query->join('uc_gc_client_schedules_adjs', 'a', 'a.sid = s.sid');
  $adjustments = $query
    ->fields('s', array('sid', 'date', 'status'))
    ->fields('a', array('title', 'amount', 'aid'))
    ->condition('s.ucpid', $gc->ucpid, '=')
    ->condition('type', 'adjustment', '=')
    ->orderBy('timestamp', 'ASC')
    ->execute()->fetchAll();

  if (!empty($adjustments)) {
    $adj_rows = array();
    foreach ($adjustments as $adj) {
      $adj->status == 1 ? $cancel = l(t('cancel'), 'admin/store/orders/' . $gc->ucid . '/gocardless/scheduled_action/cancel/' . $adj->sid . '/' . $adj->aid, array()) : $cancel = NULL;
      $adj->status == 1 ? $spacer = ' | ' : $spacer = NULL;
      $delete = l(t('delete'), 'admin/store/orders/' . $gc->ucid . '/gocardless/scheduled_action/delete/' . $adj->sid . '/' . $adj->aid, array());
      $actions = $cancel . $spacer . $delete;
      $status = array(t('Cancelled'), t('Pending'), t('Complete'))[$adj->status];
      $adj_rows[] = array(
        $adj->title,
        $adj->amount,
        $adj->date,
        $status,
        $actions,
      );
    }
  }

  $form['adjust'] = array(
    '#type' => 'fieldset',
    '#title' => t('Schedule adjustments'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#description' => '<p>' . t('Adjust the amounts for upcoming scheduled One-off payments.') . '</p>',
    '#prefix' => '<div id="adjustments"></div>',
    '#access' => $gc->type == 'P' && $product->data['interval_params']['length'] ? TRUE : FALSE,
  );
  if (isset($adj_rows)) {
    $form['adjust']['adjust_table'] = array(
      '#type' => 'fieldset',
      '#title' => t('Adjustments'),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
    );
    $adj_headers = array(
      t('Title'),
      t('Adjustment'),
      t('Date'),
      t('Status'),
      t('Actions'),
    );
    // Adjustments tables.
    $form['adjust']['adjust_table']['table'] = array(
      '#theme' => 'table',
      '#header' => $adj_headers,
      '#rows' => isset($adj_rows) ? $adj_rows : NULL,
      '#empty' => t('There are no scheduled adjustments for this product yet.'),
    );
  }
  $form['adjust']['adjust_title'] = array(
    '#type' => 'textfield',
    '#size' => 24,
    '#title' => t('Adjustment title'),
    '#autocomplete_path' => 'gc_client/schedules/autocomplete',
  );
  $form['adjust']['operator'] = array(
    '#type' => 'select',
    '#title' => t('Operation'),
    '#options' => array('add' => t('add'), 'subtract' => t('subtract')),
  );
  $form['adjust']['adjustment'] = array(
    '#type' => 'uc_price',
    '#title' => t('Adjustment amount'),
    '#description' => t("This will adjust the amount of the payment that GoCardless is instructed to create on the specified date(s). Adjustments are set in the site's default currency, regardless of the customer's country."),
  );
  $form['adjust']['payments'] = array(
    '#type' => 'textfield',
    '#size' => 6,
    '#title' => t('Number of payments'),
    '#default_value' => 1,
    '#element_validate' => array('uc_gc_client_element_validate_positive_number'),
  );
  $form['adjust']['starting_radio'] = array(
    '#type' => 'radios',
    '#title' => t('Starting from'),
    '#options' => array(
      t('Next scheduled payment creation date'),
      t('Select another date'),
    ),
    '#default_value' => 0,
  );
  $form['adjust']['starting'] = array(
    '#type' => 'date_popup',
    '#title' => t('Starting from'),
    '#description' => t('The adjustment(s) will begin on the first scheduled payment creation date after that specified here.'),
    '#date_format' => 'd M Y',
    '#date_year_range' => '0:+1',
    '#datepicker_options' => array('minDate' => 0),
    '#states' => array(
      'visible' => array(
        ':input[name="starting_radio"]' => array('value' => 1),
      ),
    ),
  );
  $form['adjust']['plus'] = array(
    '#type' => 'fieldset',
    '#title' => t('and then'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#description' => t("Additional scheduled adjustment to follow initial adjustment"),
  );
  $form['adjust']['plus']['plus_operator'] = array(
    '#type' => 'select',
    '#title' => t('Operation'),
    '#options' => array('add' => t('add'), 'subtract' => t('subtract')),
  );
  $form['adjust']['plus']['plus_adjustment'] = array(
    '#type' => 'uc_price',
    '#title' => t('Adjustment amount'),
  );
  $form['adjust']['plus']['plus_payments'] = array(
    '#type' => 'textfield',
    '#size' => 6,
    '#title' => t('Number of payments'),
    '#default_value' => 0,
  );
  $form['adjust']['adjust_button'] = array(
    '#type' => 'submit',
    '#value' => t('Schedule'),
    '#validate' => array('uc_gc_client_adjust_validate'),
    '#submit' => array('uc_gc_client_adjust_submit'),
  );
  $form['change_price'] = array(
    '#type' => 'fieldset',
    '#title' => t('Change price'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#description' => t("Make a permanent change to the product's price"),
    '#access' => $gc->type == 'P' ? TRUE : FALSE,
  );
  $form['change_price']['cost'] = array(
    '#type' => 'uc_price',
    '#title' => t('New cost'),
  );
  $form['change_price']['price'] = array(
    '#type' => 'uc_price',
    '#title' => t('New price'),
  );
  $form['change_price']['price_button'] = array(
    '#type' => 'submit',
    '#value' => t('Change price'),
    '#submit' => array('uc_gc_client_change_price_submit'),
  );

  $form['next_payment'] = array(
    '#type' => 'fieldset',
    '#title' => t('Change next scheduled payment creation date'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#description' => t('Change the date that the next scheduled payment will be created. (This is not the same date that the customer will be charged on.)'),
    '#access' => $gc->type == 'P' ? TRUE : FALSE,
  );
  $form['next_payment']['next_payment'] = array(
    '#title' => '',
    '#type' => 'date_popup',
    '#date_format' => 'd M Y H:i',
    '#default_value' => $gc->next_payment ? date('Y-m-d H:i', $gc->next_payment): NULL,
    '#date_year_range' => '0:+2',
    '#datepicker_options' => array(
      'minDate' => 0,
    ),
  );
  $form['next_payment']['next_payment_button'] = array(
    '#type' => 'submit',
    '#value' => t('Change date'),
    '#submit' => array('uc_gc_client_next_payment_submit'),
  );
  return $form;
}

/**
 * AJAX callback function for uc_gc_client_payments_form.
 */
function uc_gc_client_payments_form_callback(&$form, &$form_state) {
  $order_id = $form_state['values']['order_id'];
  $_SESSION['uc_gc_client_change_ucpid_' . $order_id]['ucpid'] = $form_state['values']['ucpid'];
  $form['ucpid']['#value'] = $form_state['values']['ucpid'];
  return $form;
}

/**
 * Form validation handler for Scheduled Adustments.
 */
function uc_gc_client_adjust_validate($form, &$form_state) {
  if ($form_state['values']['adjustment'] == 0) {
    form_set_error('adjustment', t('Adjustment cannot be set to zero'));
  }
}

/**
 * Form submission handler for Scheduled Adustments.
 */
function uc_gc_client_adjust_submit($form, &$form_state) {
  $ucpid = $form_state['values']['ucpid'];
  $order_id = $form_state['values']['order_id'];
  $order = uc_order_load($order_id);
  $product = $order->products[$ucpid];
  $sub = db_select('uc_gc_client', 'u')
    ->fields('u')
    ->condition('ucpid', $ucpid)
    ->execute()->fetch();

  // Prepare data.
  $int_length = $product->data['interval_params']['length'];
  $int_unit = $product->data['interval_params']['unit'];
  $starting = $sub->next_payment;
  if ($form_state['values']['starting_radio'] != 0) {
    $select_date = strtotime($form_state['values']['starting']);
    while ($starting < $select_date) {
      $string = '+' . $product->data['interval_params']['string'];
      $starting = strtotime($string, $starting);
    }
  }

  // Create array containing scheduled dates.
  $payments = $form_state['values']['payments'];
  $dates = array();
  for ($i = 0; $i < $payments; $i++) {
    $string = '+' . ($i * $int_length) . ' ' . $int_unit;
    $dates[] = date('D d M Y', strtotime($string, $starting));
    $ending = strtotime($string, $starting);
  }

  // Make sure sum of order_total and all plus_adjustments for each billdate
  // are not <= 0 and are not > amount remaining.
  $form_state['values']['operator'] == 'add' ? $amount = $form_state['values']['adjustment'] : $amount = 0 - $form_state['values']['adjustment'];

  $warning = uc_gc_client_validate_adjs($order, $ucpid, $dates, $amount);
  if ($warning) {
    return;
  }

  if ($form_state['values']['plus_adjustment'] != 0) {
    // Create array containing additional scheduled dates.
    $plus_starting = strtotime('+' . $int_length . ' ' . $int_unit, $ending);
    $plus_payments = (int) $form_state['values']['plus_payments'];
    $plus_dates = array();
    for ($i = 0; $i < $plus_payments; $i++) {
      $string = '+' . ($i * $int_length) . ' ' . $int_unit;
      $plus_dates[] = date('D d M Y', strtotime($string, $plus_starting));
    }

    // Validate plus_adjustment.
    $form_state['values']['plus_operator'] == 'add' ? $plus_amount = $form_state['values']['plus_adjustment'] : $plus_amount = 0 - $form_state['values']['plus_adjustment'];
    $plus_warning = uc_gc_client_validate_adjs($order, $ucpid, $plus_dates, $plus_amount);
    if ($plus_warning) {
      return;
    }
  }

  // Add schedules data to database.
  foreach ($dates as $date) {
    $insert = db_insert('uc_gc_client_schedules')
      ->fields(array(
        'ucid' => $order_id,
        'ucpid' => $ucpid,
        'type' => 'adjustment',
        'date' => $date,
        'timestamp' => strtotime($date),
        'status' => 1,
        'created' => REQUEST_TIME,
      ))
      ->execute();

    db_insert('uc_gc_client_schedules_adjs')
      ->fields(array(
        'sid' => $insert,
        'title' => !empty($form_state['values']['adjust_title']) ? $form_state['values']['adjust_title'] : 'Adjustment',
        'amount' => $amount,
      ))
      ->execute();
  }

  if ($form_state['values']['plus_adjustment'] != 0) {
    foreach ($plus_dates as $plus_date) {
      $plus_insert = db_insert('uc_gc_client_schedules')
        ->fields(array(
          'ucid' => $order_id,
          'ucpid' => $ucpid,
          'type' => 'adjustment',
          'date' => $plus_date,
          'timestamp' => strtotime($plus_date),
          'status' => 1,
          'created' => REQUEST_TIME,
        ))
        ->execute();

      db_insert('uc_gc_client_schedules_adjs')
        ->fields(array(
          'sid' => $plus_insert,
          'title' => $form_state['values']['adjust_title'],
          'amount' => $plus_amount,
        ))
        ->execute();
    }
  }
  backdrop_set_message(t('New Scheduled Adjustment successfully added'));
}

/**
 * Checks validity of scheduled adjustments.
 *
 * Checks against existing scheduled adjustments, and any alterations from
 * other modules to determine if the price is greated than 1 and not zero.
 *
 * @param object $order
 *   The Ubercart order that the scheduled adjustment is being created under.
 * @param int $ucpid
 *   The Ubercart product ID that the scheduled adjustment is for.
 * @param array $dates
 *   One or more payment creation dates, as strings, that the adjustment will
 *   be applied to.
 * @param float $amount
 *   The amount of the adjustment.
 *
 * @return bool
 *   Returns TRUE if the scheduled adjustment is invalid.
 *
 * @see uc_gc_client_adjust_submit()
 */
function uc_gc_client_validate_adjs($order, $ucpid, array $dates, $amount) {
  foreach ($dates as $date) {
    // Calculate sum of scheduled adjs for date.
    $db_query = "SELECT s.ucid, s.date, sum( a.amount ) AS sum FROM {uc_gc_client_schedules} s inner join {uc_gc_client_schedules_adjs} a on s.sid = a.sid WHERE s.status = 1 AND s.date = '" . $date . "' AND s.ucpid = " . $order->order_id;
    $result = db_query($db_query)
      ->fetch();
    $sum = is_null($result->sum) ? 0 : $result->sum;

    // Provide a hook so that $amount and/or $price can be altered by another
    // module.
    $price = $order->products[$ucpid]->price;
    backdrop_alter('adjs_validate', $amount, $price, $date, $order->order_id);
    $sum = $sum + $amount + $price;

    $warning = FALSE;
    if ($sum < 1 && $sum != 0) {
      backdrop_set_message(t('The schedule cannot be placed because the sum of scheduled adjustments, plus the price of the product, for @date is less than @amount, which is not allowed by GoCardless', array(
        '@date' => $date,
        '@amount' => uc_currency_format(1),
      )), 'warning');
      $warning = TRUE;
    }
    if ($warning) {
      return $warning;
    }
  }
}

/**
 * Page callback; confirmation form for scheduled adjustment actions.
 *
 * @param object $order
 *   The Ubercart Order object of the scheduled adjustment.
 * @param string $action
 *   The action being performed on the scheduled adjustment, either 'cancel'
 *   or 'delete'.
 * @param int $sid
 *   The schedule ID of the scheduled adjustment.
 * @param int $aid
 *   The action ID of the scheduled adjustment.
 *
 * @see uc_gc_client_scheduled_action_submit()
 */
function uc_gc_client_scheduled_action($form, &$form_state, $order, $action, $sid, $aid) {
  $form['action'] = array(
    '#type' => 'value',
    '#value' => $action,
  );
  $form['ucid'] = array(
    '#type' => 'value',
    '#value' => $order->order_id,
  );
  $form['sid'] = array(
    '#type' => 'value',
    '#value' => $sid,
  );
  $form['aid'] = array(
    '#type' => 'value',
    '#value' => $aid,
  );
  $path = array(
    'path' => 'admin/store/orders/' . $order->order_id . '/gocardless',
    'fragment' => 'adjustments',
  );
  return confirm_form($form, t('Are you sure you want to %action the scheduled adjustment?', array(
    '%action' => $action,
  )), $path, FALSE, t('@action scheduled adjustment', array('@action' => ucfirst($action))), t('Cancel'));
}

/**
 * Form submission handler.
 *
 * Cancels or deletes a scheduled adjustment.
 */
function uc_gc_client_scheduled_action_submit($form, &$form_state) {
  $action = $form_state['values']['action'];
  $sid = $form_state['values']['sid'];
  $aid = $form_state['values']['aid'];
  if ($action == 'cancel') {
    db_update('uc_gc_client_schedules')
      ->fields(array(
        'status' => 0,
      ))
      ->condition('sid', $sid)
      ->execute();
  }
  if ($action == 'delete') {
    db_delete('uc_gc_client_schedules')
      ->condition('sid', $sid)
      ->execute();
    db_delete('uc_gc_client_schedules_adjs')
      ->condition('aid', $aid)
      ->execute();
  }
  $action == 'cancel' ? $action = 'cancelled' : $action = 'deleted';
  backdrop_set_message(t('Scheduled adjustment @action.', array(
    '@action' => $action,
  )));
  $path = 'admin/store/orders/' . $form_state['values']['ucid'] . '/gocardless';
  backdrop_goto($path, array('fragment' => 'adjustments'));
}

/**
 * Form validation handler for Create Payment.
 */
function uc_gc_client_create_payment_validate($form, &$form_state) {
  $mandate = $form_state['values']['mandate'];
  $amount = $form_state['values']['payment_amount'];
  $order_id = $form_state['values']['order_id'];
  $order = uc_order_load($order_id);
  $ucpid = $form_state['values']['ucpid'];
  $data = array('amount' => $amount);
  $calculate = uc_gc_client_price_calculate($order, $ucpid, FALSE, FALSE, $data); 
  $form_state['storage']['calculate'] = $calculate;
  if ($amount == 0) {
    form_set_error('payment_amount', t('Please provide an amount'));
  }
  elseif ($calculate['amount'] < 1) {
    form_set_error('payment_amount', t('The minimum payment amount for GoCardless is @amount', array(
      '@amount' => uc_currency_format(1, $calculate['sign']),
    )));
  }

  // Check that specified date is greater than or equal to the
  // next possible charge date.
  if (!is_null($form_state['values']['charge_date'])) {
    if (strtotime($form_state['values']['charge_date']) < strtotime($mandate->next_possible_charge_date)) {
      form_set_error('charge_date', t('The date cannot be before the Next Possible Charge Date.'));
    }
  }
}

/**
 * Form submission handler for Create Payment.
 */
function uc_gc_client_create_payment_submit($form, &$form_state) {
  $mandate = $form_state['values']['mandate'];
  $amount = $form_state['values']['payment_amount'];
  $order_id = $form_state['values']['order_id'];
  $order = uc_order_load($order_id);
  $ucpid = $form_state['values']['ucpid'];
  $calculate = $form_state['storage']['calculate'];
  $params = array(
    'endpoint' => 'payments',
    'action' => 'create',
    'mandate' => $mandate->id,
    'amount' => $calculate['amount'],
    'currency' => isset($calculate['currency']) ? $calculate['currency'] : config_get('uc_store.settings', 'uc_currency_code'),
    'description' => !empty($form_state['values']['payment_title']) ? $form_state['values']['payment_title'] : $order->products[$ucpid]->title,
    'charge_date' => $form_state['values']['charge_date'],
    'metadata' => array(
      'ucpid' => $ucpid,
    ),
  );
  $result = uc_gc_client_api($params);
  $payment = $result->response->body->payments;

  $text = t('A one-off payment for @amount has been created by Admin with GoCardless mandate @mandate.', array(
    '@amount' => uc_currency_format($calculate['amount'], $calculate['sign']),
    '@mandate' => $mandate->id,
  ));
  backdrop_set_message(t('@text', array('@text' => $text)));
  uc_order_comment_save($order->order_id, $order->uid, $text, 'order');
  $watchdog = t('Order #@order_id:', array(
    '@order_id' => $order->order_id,
  )) . ' ' . $text;
  watchdog('uc_gc_client', $watchdog, array(), WATCHDOG_INFO);

  // Provide a hook so that other modules can respond to payment creation.
  module_invoke_all('payment_created', $payment, $order->order_id, 'manual');
}

/**
 * Page callback: Provides a confirmation form for cancelling payments.
 *
 * @param object $order
 *   The Ubercart Order object of the scheduled adjustment.
 * @param int $payment_id
 *   The GoCardless ID for the payment.
 *
 * @see uc_gc_client_cancel_payment_submit()
 */
function uc_gc_client_cancel_payment($form, &$form_state, $order, $payment_id) {
  $form['ucid'] = array(
    '#type' => 'value',
    '#value' => $order->order_id,
  );
  $form['payment_id'] = array(
    '#type' => 'value',
    '#value' => $payment_id,
  );
  $path = array(
    'path' => 'admin/store/orders/' . $order->order_id . '/gocardless',
    'fragment' => 'payment',
    'query' => array('payment' => 'unchanged',
    ),
  );
  return confirm_form($form, t('Are you sure you want to cancel the payment?'), $path, NULL, t('Cancel payment'), t('Go back'));
}

/**
 * Form submission handler.
 *
 * Cancels a payment at GoCardless.
 */
function uc_gc_client_cancel_payment_submit($form, &$form_state) {
  $payment_id = $form_state['values']['payment_id'];
  $params = array(
    'endpoint' => 'payments',
    'action' => 'cancel',
    'id' => $payment_id,
  );
  $result = uc_gc_client_api($params);
  if ($result->response->status_code == 200) {
    backdrop_set_message(t('Payment @payment_id has been cancelled.', array(
      '@payment_id' => $payment_id,
    )));
  }
  else {
    backdrop_set_message(t('There was a problem cancelling payment @payment_id', array(
      '@payment_id' => $payment_id,
    )), 'warning');
  }
  $link = 'admin/store/orders/' . $form_state['values']['ucid'] . '/gocardless';
  backdrop_goto($link);
}

/**
 * Form submission handler for Next Payment.
 */
function uc_gc_client_next_payment_submit($form, &$form_state) {
  $next_payment = NULL;
  if ($form_state['values']['next_payment']) {
    $next_payment = strtotime($form_state['values']['next_payment']);
  }
  $ucpid = $form_state['values']['ucpid'];
  $title = $form_state['values']['product_select'][$ucpid];

  db_update('uc_gc_client')
    ->fields(array(
      'next_payment' => $next_payment,
    ))
    ->condition('ucpid', $ucpid)
    ->execute();

  backdrop_set_message(t('The next payment creation date for @title has been @next_payment.', array(
    '@title' => $title,
    '@next_payment' => $next_payment ? 'updated to ' . format_date($next_payment, 'medium') : 'unset',
  )));
}

/**
 * Form submission handler for Change Price.
 */
function uc_gc_client_change_price_submit($form, &$form_state) {
  $cost = $form_state['values']['cost'];
  $price = $form_state['values']['price'];
  $order_id = $form_state['values']['order_id'];
  $ucpid = $form_state['values']['ucpid'];
  $order = uc_order_load($order_id);
  $gc = db_select('uc_gc_client', 'u')
    ->fields('u', array('ucpid', 'next_payment'))
    ->condition('ucpid', $ucpid)
    ->execute()->fetch();
  $price_old = uc_currency_format($order->products[$gc->ucpid]->price);
  $price_new = uc_currency_format($price);
  $order->products[$gc->ucpid]->cost = $cost;
  $order->products[$gc->ucpid]->price = $price;
  $title = $order->products[$gc->ucpid]->title;
  $next = format_date($gc->next_payment, 'uc_store');
  uc_order_save($order);

  $text = t('The price for @title has been changed permanently from @price_old to @price_new. The next payment is scheduled for @next.', array(
    '@title' => $title,
    '@price_old' => $price_old,
    '@price_new' => $price_new,
    '@next' => $next,
  ));
  backdrop_set_message(t('@text', array('@text' => $text)));
  uc_order_comment_save($order->order_id, $order->uid, $text, 'order');
  $watchdog = t('Order #@order_id:', array(
    '@order_id' => $order->order_id,
  )) . ' ' . $text;
  watchdog('uc_gc_client', $watchdog, array(), WATCHDOG_INFO);
}

/**
 * Menu callback function.
 */
function uc_gc_client_schedules_autocomplete($string) {
  $matches = array();
  $titles = explode("\n", config_get('uc_gc_client.settings', 'preauth_adjs'));
  foreach ($titles as $title) {
    if (strpos(strtolower($title), strtolower($string)) !== FALSE) {
      $matches[$title] = check_plain($title);
    }
  }
  backdrop_json_output($matches);
}

/**
 * Sorts payments by created_at date.
 *
 * Used where one-off payments have been mixed with subsription payments.
 */
function uc_gc_client_sort_function($a, $b) {
  return strtotime($a->created_at) - strtotime($b->created_at);
}
